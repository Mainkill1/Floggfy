#define WIN32_LEAN_AND_MEAN
#include "audio_history.h"
#include "ogg_history_core.h"
#include "ogg_tags.h"
#include "flac_history_core.h"
#include "history_settings.h"
#include "hook_init_state.h"
#include "hook_rollback.h"
#include "metadata_bridge.h"
#include "playback_quality.h"
#include "auto_skip.h"
#include "async_log.h"
#include "hook_diagnostics.h"
#include <array>
#include "bounded_queue.h"
#include "srw_guard.h"
#include "connectivity_diagnostics.h"
#include <optional>
#include <atomic>
#include "library_layout.h"
#include "existing_quality.h"
#include "file_publication.h"
#include "recent_downloads.h"
#include "song_destination.h"
#include "metadata_update_windows.h"
#include "metadata_update_core.h"
#include "media_session.h"
#include "recent_playback.h"
#include "image_info.h"
#include "aac_stream.h"
#include "mp3_stream.h"
#include "spotify_hook_discovery.h"
#include "decoded_output_speed.h"
#include "decoder_ownership.h"
#include "playback_identity_bridge.h"
#include "recording_formats.h"
#include "recording_file.h"
#include "vendor/minhook/include/MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <memory>
#include <vector>

namespace {
using namespace history;
struct OggPage { unsigned char* header; int32_t header_length; unsigned char* body; int32_t body_length; };
static_assert(sizeof(OggPage)==32 && offsetof(OggPage,body)==16,"Windows x64 libogg ABI");
using PageSeek = int32_t(*)(void*,OggPage*);
PageSeek original;
void* target;
using FlacInit=void*(*)(void*,void*);
using FlacRead=int(*)(void*,unsigned char*,size_t*);
using FlacFrame=int(*)(void*,const void*,const void*);
using FlacError=void(*)(void*,unsigned);
FlacInit original_flac_init;
FlacRead original_flac_read;
FlacFrame original_flac_frame;
FlacError original_flac_error;
void* flac_targets[4]={};
struct AacView {const unsigned char* bytes;size_t size;};
static_assert(sizeof(AacView)==16,"Windows x64 SpacReader input view ABI");
using AacInitialize=void*(*)(void*,bool);
using AacPush=void*(*)(void*,void*,const AacView*);
using AacDestroy=void*(*)(void*,unsigned);
AacInitialize original_aac_initialize=nullptr;
AacPush original_aac_push=nullptr;
AacDestroy original_aac_destroy=nullptr;
void* aac_targets[3]={};
using DecodedProcess=void*(*)(void*,void*,decoded_output::Span*,decoded_output::Span*,bool);
DecodedProcess original_decoded_process=nullptr;
void* decoded_sink_target=nullptr;
using PlaybackPump=int(*)(void*,void*,void*);
PlaybackPump original_playback_pump=nullptr;
void* playback_pump_target=nullptr;
std::int32_t playback_pump_decoder_offset=0;
playback_identity::DiagnosticBridge playback_identity_bridge;
SRWLOCK playback_identity_lock=SRWLOCK_INIT;
std::atomic<std::uint64_t> playback_identity_sequence{0};
std::uint64_t playback_identity_metadata_generation=0;
decoded_output::Accelerator decoded_accelerator;
std::atomic<bool> decoded_fallback_requested{false};
enum class DecodedFallbackReason : unsigned {
    None,MissingSpan,DecodedSpan,EncodedSpan,MissingOwner,OddSamples,UnsupportedSpeed
};
std::atomic<unsigned> decoded_fallback_reason{0};
spotify::DecoderOwnershipLayout decoder_ownership_layout{};
bool decoder_ownership_layout_available=false;
hooks::CallbackCounter audio_callbacks;
enum class AudioEventKind : unsigned { OggPage,FlacBegin,FlacBytes,FlacFrame,FlacError,FlacEnd,
    AacBegin,AacBytes,AacError,AacDestroy,Mp3Begin,Mp3Bytes };
enum class FlacErrorSource : unsigned { None,ReadCallback,FrameCallback,DecoderCallback };
struct Slot { uintptr_t context; unsigned length; double time; AudioEventKind kind;unsigned epoch; uint64_t input_length;
    uint64_t sequence;
    uint32_t format; bool capture;
    FlacErrorSource error_source;
    uint64_t requested,returned; unsigned char bytes[65307]; };
constexpr size_t max_decoded_callback_bytes=1024*1024;
constexpr size_t max_decoded_callback_floats=1024*1024;
// A 10x decoder can deliver more than 128 maximum-sized compressed callbacks
// while the worker performs final cache reconciliation. Keep that bounded burst
// in RAM instead of invalidating an otherwise complete next capture.
constexpr unsigned capacity=256;
Slot* queue;
unsigned head=0,tail=0,count=0;
SRWLOCK queue_lock=SRWLOCK_INIT;
HANDLE event;
volatile LONG dropped=0;
struct DecodedActivitySlot {
    uintptr_t context=0;
    uint64_t calls=0;
    uint64_t produced_floats=0;
    uint64_t consumed_bytes=0;
    uint64_t kept_floats=0;
    double first=0;
    double last=0;
    unsigned epoch=0;
    uint64_t first_sequence=0;
    uint64_t last_sequence=0;
};
constexpr unsigned decoded_activity_capacity=256;
std::array<DecodedActivitySlot,decoded_activity_capacity> decoded_activity_queue{};
unsigned decoded_activity_head=0,decoded_activity_tail=0,decoded_activity_count=0;
volatile LONG decoded_activity_dropped=0;
LONG decoded_activity_loss_seen=0;
uint64_t producer_sequence=0;
enum class Mp3ProbeState : unsigned char { Empty,Probing,Capturing,Rejected };
struct Mp3ProbeSlot {
    uintptr_t context=0;unsigned epoch=0;Mp3ProbeState state=Mp3ProbeState::Empty;
    unsigned length=0;unsigned char bytes[10]{};
};
std::array<Mp3ProbeSlot,32> mp3_probes{};
unsigned mp3_probe_next=0;
std::array<uint64_t,16> ownership_output_logged{};
unsigned ownership_output_logged_count=0,ownership_output_logged_next=0;
std::atomic<bool> quality_enabled{false};
std::wstring output;
size_t memory_limit=500*1024*1024;
static double Now() { return double(GetTickCount64())/1000.0; }
static void Log(LogKind kind,const char* message) {
    LogEvent(kind,kind==LogKind::Diagnostic?"audio":kind==LogKind::Warning?"warning":
        kind==LogKind::Failure?"failed":"activity",message);
}
// Hooks only record numeric counters/POD events. Formatting and disk queuing
// belong to the worker. Independent exchanges make interval metrics approximate
// under concurrency; maxima are latency evidence, not a tracing profiler.
struct TimingSample {
    std::atomic<uint64_t> count{0},maximum{0};
    void Add(uint64_t ticks) noexcept {
        count.fetch_add(1,std::memory_order_relaxed);
        auto prior=maximum.load(std::memory_order_relaxed);
        for(unsigned attempt=0;attempt<4 && prior<ticks;++attempt)
            if(maximum.compare_exchange_weak(prior,ticks,std::memory_order_relaxed))break;
    }
};
static TimingSample native_read_timing,native_frame_timing,native_error_timing,native_init_timing,native_ogg_timing;
static TimingSample producer_wait_timing,producer_hold_timing,consumer_hold_timing;
static std::atomic<uint64_t> empty_flac_reads{0};
static uint64_t DiagnosticTick() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value)?uint64_t(value.QuadPart):0;
}
struct NativeTimer {
    TimingSample& sample;
    uint64_t began;
    explicit NativeTimer(TimingSample& target):sample(target),began(HookDiagnosticsEnabled()?DiagnosticTick():0) {}
    void Finish() noexcept {
        if(began) {const auto end=DiagnosticTick();if(end>=began)sample.Add(end-began);began=0;}
    }
};
struct QueueGuard {
    uint64_t began,acquired;
    bool producer;
    explicit QueueGuard(bool writing=true):began(HookDiagnosticsEnabled()?DiagnosticTick():0),acquired(0),producer(writing) {
        AcquireSRWLockExclusive(&queue_lock);
        if(began)acquired=DiagnosticTick();
    }
    ~QueueGuard() {
        const auto end=acquired?DiagnosticTick():0;
        ReleaseSRWLockExclusive(&queue_lock);
        // Count after releasing: metric updates never extend the critical section.
        if(acquired>=began && began && end>=acquired) {
            if(producer) {producer_wait_timing.Add(acquired-began);producer_hold_timing.Add(end-acquired);}
            else consumer_hold_timing.Add(end-acquired);
        }
    }
    QueueGuard(const QueueGuard&)=delete;
    QueueGuard& operator=(const QueueGuard&)=delete;
};
static unsigned SlotsFor(size_t bytes) noexcept {
    return bytes?1+unsigned((bytes-1)/sizeof(Slot::bytes)):0;
}
static void PushMp3SlotLocked(uintptr_t context,AudioEventKind kind,const unsigned char* bytes,
                              size_t length,double time,unsigned epoch) noexcept {
    auto& slot=queue[tail];slot.context=context;slot.kind=kind;slot.length=unsigned(length);
    slot.time=time;slot.epoch=epoch;slot.capture=true;slot.sequence=++producer_sequence;
    if(length)memcpy(slot.bytes,bytes,length);
    tail=(tail+1)%capacity;++count;
}
static void PushMp3BytesLocked(uintptr_t context,const unsigned char* bytes,size_t length,
                               double time,unsigned epoch) noexcept {
    while(length) {
        const auto amount=std::min(length,sizeof(Slot::bytes));
        PushMp3SlotLocked(context,AudioEventKind::Mp3Bytes,bytes,amount,time,epoch);
        bytes+=amount;length-=amount;
    }
}
static bool Mp3Header(const unsigned char* bytes) noexcept {
    if(bytes[0]!=0xff||(bytes[1]&0xe0)!=0xe0)return false;
    const unsigned version=(bytes[1]>>3)&3,layer=(bytes[1]>>1)&3;
    const unsigned bitrate=(bytes[2]>>4)&15,rate=(bytes[2]>>2)&3;
    return version!=1&&layer==1&&bitrate&&bitrate!=15&&rate!=3;
}
static Mp3ProbeSlot* Mp3Probe(uintptr_t context,unsigned epoch) noexcept {
    for(auto& probe:mp3_probes)if(probe.context==context&&probe.epoch==epoch)return &probe;
    for(auto& probe:mp3_probes)if(probe.state==Mp3ProbeState::Empty) {
        probe={};probe.context=context;probe.epoch=epoch;probe.state=Mp3ProbeState::Probing;return &probe;
    }
    auto& probe=mp3_probes[mp3_probe_next++%mp3_probes.size()];
    if(probe.state==Mp3ProbeState::Capturing)InterlockedIncrement(&dropped);
    probe={};probe.context=context;probe.epoch=epoch;probe.state=Mp3ProbeState::Probing;return &probe;
}
static void QueueMp3Encoded(uintptr_t context,const unsigned char* bytes,size_t length,
                            double time,unsigned epoch) noexcept {
    if(!queue||!context||!bytes||!length)return;
    if(length>max_decoded_callback_bytes){InterlockedIncrement(&dropped);if(event)SetEvent(event);return;}
    QueueGuard guard;auto* probe=Mp3Probe(context,epoch);if(!probe)return;
    if(probe->state==Mp3ProbeState::Rejected)return;
    if(probe->state==Mp3ProbeState::Capturing) {
        const auto needed=SlotsFor(length);
        if(needed>capacity-count){InterlockedIncrement(&dropped);return;}
        PushMp3BytesLocked(context,bytes,length,time,epoch);if(event)SetEvent(event);return;
    }
    const auto copied=std::min(length,sizeof(probe->bytes)-probe->length);
    memcpy(probe->bytes+probe->length,bytes,copied);probe->length+=unsigned(copied);
    const bool id3=probe->length>=3&&!memcmp(probe->bytes,"ID3",3);
    const bool mp3=probe->length>=4&&Mp3Header(probe->bytes);
    if(!id3&&!mp3) {
        if(probe->length>=4)probe->state=Mp3ProbeState::Rejected;
        return;
    }
    const auto remaining=length-copied;
    const auto needed=1+SlotsFor(probe->length)+SlotsFor(remaining);
    if(needed>capacity-count){InterlockedIncrement(&dropped);probe->state=Mp3ProbeState::Rejected;return;}
    probe->state=Mp3ProbeState::Capturing;
    PushMp3SlotLocked(context,AudioEventKind::Mp3Begin,nullptr,0,time,epoch);
    PushMp3BytesLocked(context,probe->bytes,probe->length,time,epoch);
    if(remaining)PushMp3BytesLocked(context,bytes+copied,remaining,time,epoch);
    if(event)SetEvent(event);
}
static void ReleaseMp3Probe(uintptr_t context,unsigned epoch) noexcept {
    AcquireSRWLockExclusive(&queue_lock);
    for(auto& probe:mp3_probes)if(probe.context==context&&probe.epoch==epoch){probe={};break;}
    ReleaseSRWLockExclusive(&queue_lock);
}
static void ResetMp3Probes() noexcept {
    AcquireSRWLockExclusive(&queue_lock);mp3_probes={};mp3_probe_next=0;ReleaseSRWLockExclusive(&queue_lock);
}
static void LogTiming() {
    if(!HookDiagnosticsEnabled())return;
    LARGE_INTEGER frequency{};
    if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return;
    const double milliseconds=1000.0/double(frequency.QuadPart);
    struct Metric {
        const char* name;
        TimingSample* sample;
        uint64_t count=0;
        double maximum_ms=0;
    };
    std::array<Metric,8> metrics{{
        {"native-read",&native_read_timing},{"native-frame",&native_frame_timing},
        {"native-error",&native_error_timing},{"native-init",&native_init_timing},
        {"native-ogg",&native_ogg_timing},{"queue-producer-wait",&producer_wait_timing},
        {"queue-producer-hold",&producer_hold_timing},{"queue-consumer-hold",&consumer_hold_timing}
    }};
    size_t worst=0;
    for(size_t i=0;i<metrics.size();++i) {
        auto& metric=metrics[i];
        metric.count=metric.sample->count.exchange(0,std::memory_order_relaxed);
        metric.maximum_ms=double(metric.sample->maximum.exchange(0,std::memory_order_relaxed))*milliseconds;
        if(metric.maximum_ms>metrics[worst].maximum_ms)worst=i;
    }
    const auto empty=empty_flac_reads.exchange(0,std::memory_order_relaxed);
    // Reporting threshold only: this neither rejects audio nor diagnoses a pause.
    // Profiling intervals drain counters without a per-metric heartbeat.
    constexpr double report_ms=10;
    if(metrics[worst].maximum_ms<report_ms&&!empty)return;
    const char* context=metrics[worst].maximum_ms>=report_ms?"slow":"empty-read";
    char line[600];snprintf(line,sizeof(line),
        "hook timing reason=%s worst=%s count=%llu max_ms=%.3f threshold_ms=10 native_read_ms=%.3f native_frame_ms=%.3f native_error_ms=%.3f native_init_ms=%.3f native_ogg_ms=%.3f queue_wait_ms=%.3f producer_hold_ms=%.3f consumer_hold_ms=%.3f empty_reads=%llu callbacks_now=%u",
        context,metrics[worst].maximum_ms>0?metrics[worst].name:"none",
        static_cast<unsigned long long>(metrics[worst].count),metrics[worst].maximum_ms,
        metrics[0].maximum_ms,metrics[1].maximum_ms,metrics[2].maximum_ms,metrics[3].maximum_ms,
        metrics[4].maximum_ms,metrics[5].maximum_ms,metrics[6].maximum_ms,metrics[7].maximum_ms,
        static_cast<unsigned long long>(empty),audio_callbacks.Active());
    Log(LogKind::Diagnostic,line);
}
static void LogConnectivity() {
    if(!DebugLoggingEnabled())return;
    const auto value=LastConnectivity();
    if(!value.known)return;
    static ConnectivityObservation previous;
    if(previous.known&&previous.flags_valid==value.flags_valid&&previous.result==value.result&&
       previous.original==value.original&&previous.effective==value.effective)return;
    previous=value;
    const auto now=GetTickCount64();
    char line[240];snprintf(line,sizeof(line),
        "network observation=last-NLM age_ms=%llu hr=0x%08lX flags_known=%d original=0x%lX effective=0x%lX; stream reachability unknown",
        static_cast<unsigned long long>(now>=value.observed_ms?now-value.observed_ms:0),
        static_cast<unsigned long>(uint32_t(value.result)),value.flags_valid,
        static_cast<unsigned long>(value.original),static_cast<unsigned long>(value.effective));Log(LogKind::Diagnostic,line);
}
static const char* PlaybackStatus(int value) {
    switch(value) {
        case 0:return "closed";case 1:return "opened";case 2:return "changing";
        case 3:return "stopped";case 4:return "playing";case 5:return "paused";
        default:return "unknown";
    }
}
struct PlaybackDiagnostics {
    bool known=false,playing=false;
    int status=-1;
    std::string key;
    double position=0,observed=0;
    void Observe(const Media& media,double at,uint64_t sequence) {
        if(!DebugLoggingEnabled()){known=false;return;}
        const auto next_key=media.Key();
        const bool changed=known && next_key!=key;
        const double elapsed=known?at-observed:0;
        const double movement=known?media.position-position:0;
        const bool jump=known&&!changed&&(movement<-.75 || movement>std::max(0.0,elapsed)+2);
        if(!known||changed||media.playing!=playing||media.playback_status!=status||jump) {
            char line[380];snprintf(line,sizeof(line),
                "playback observation=%s state=%s raw=%d playing=%d pos=%.3f/%.3f movement=%.3f elapsed=%.3f mono=%.3f seq=%llu track_changed=%d; initiator=unknown",
                !known?"baseline":jump?"timeline-jump":"transition",PlaybackStatus(media.playback_status),media.playback_status,
                media.playing,media.position,media.duration,movement,elapsed,at,
                static_cast<unsigned long long>(sequence),changed);Log(LogKind::Diagnostic,line);
            LogConnectivity();
        }
        known=true;playing=media.playing;status=media.playback_status;
        key=next_key;position=media.position;observed=at;
    }
};
// Profiling is independent of playback state: paused/idle periods still drain
// interval counters, but basic diagnostics never query callback timers.
static void DrainTiming(double now,double& deadline) {
    if(!HookDiagnosticsEnabled()||now<deadline)return;
    LogTiming();deadline=now+10;
}
static bool Directories(const std::wstring& path) {
    return EnsureDirectory(path);
}
static spotify::DiscoveryResult DiscoverTargets(HMODULE module,spotify::HookTargets& targets) {
    auto* base=reinterpret_cast<unsigned char*>(module);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096)
        return spotify::DiscoveryResult::InvalidImage;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)
        return spotify::DiscoveryResult::InvalidImage;
    return spotify::DiscoverHookTargets(base,nt->OptionalHeader.SizeOfImage,targets);
}

static const char* DiscoveryFailure(spotify::DiscoveryResult result) {
    switch(result) {
        case spotify::DiscoveryResult::InvalidImage:return "invalid PE image";
        case spotify::DiscoveryResult::MissingTarget:return "required instruction anchor missing";
        case spotify::DiscoveryResult::AmbiguousTarget:return "instruction anchor is ambiguous";
        case spotify::DiscoveryResult::InconsistentLayout:return "target layout failed structural checks";
        case spotify::DiscoveryResult::Found:return "none";
    }
    return "unknown discovery failure";
}

static void* DiscoveredTarget(HMODULE module,const spotify::HookTargets& targets,
                              spotify::TargetKind kind) {
    const auto rva=spotify::TargetRva(targets,kind);
    return rva?reinterpret_cast<unsigned char*>(module)+rva:nullptr;
}
static int32_t Hook(void* sync,OggPage* page) {
    auto callback=audio_callbacks.Enter();
    NativeTimer timing(native_ogg_timing);
    int32_t result=original(sync,page);timing.Finish();
    const bool capture=OggEnabled();
    if(!capture && !quality_enabled.load(std::memory_order_relaxed)) return result;
    if(result<=0 || !page || page->header_length<27 || page->header_length>282 ||
       page->body_length<0 || page->body_length>65025 ||
       page->header_length+page->body_length!=result || result>65307) return result;
    {
        QueueGuard guard;
        if(count==capacity) InterlockedIncrement(&dropped);
        else {
            Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(sync);
            s.length=unsigned(result); s.time=Now(); s.kind=AudioEventKind::OggPage; s.epoch=CaptureEpoch(); s.capture=capture;
            s.sequence=++producer_sequence;
            std::memcpy(s.bytes,page->header,page->header_length);
            if(page->body_length) std::memcpy(s.bytes+page->header_length,page->body,page->body_length);
            tail=(tail+1)%capacity; ++count;
        }
    }
    SetEvent(event); return result;
}
static bool Pop(Slot& s) {
    bool ready=false;
    {
        QueueGuard guard(false);
        ready=count!=0;
        if(ready) { s=queue[head]; head=(head+1)%capacity; --count; }
    }
    return ready;
}
static void FlacEvent(void* context,AudioEventKind kind,const void* bytes=nullptr,size_t n=0,unsigned status=0,
                      FlacErrorSource source=FlacErrorSource::None,size_t requested=0,size_t returned=0) {
    const bool capture=FlacEnabled();
    if(!capture && !quality_enabled.load(std::memory_order_relaxed)) return;
    {
        QueueGuard guard;
        do {
            unsigned amount=unsigned(std::min(n,size_t(65307)));
            if(count==capacity) { InterlockedIncrement(&dropped); break; }
            Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(context); s.kind=kind;
            s.capture=capture; s.input_length=amount;
            s.length=(!capture && kind==AudioEventKind::FlacBytes) ? std::min(amount,42u) : amount;
            s.time=Now(); s.format=status; s.epoch=CaptureEpoch();
            s.sequence=++producer_sequence;
            s.error_source=source;s.requested=requested;s.returned=returned;
            if(s.length) memcpy(s.bytes,bytes,s.length);
            tail=(tail+1)%capacity; ++count;
            n-=amount;
            if(n) bytes=static_cast<const unsigned char*>(bytes)+amount;
        } while(n);
    }
    SetEvent(event);
}
static void* FlacInitHook(void* context,void* result) {
    auto callback=audio_callbacks.Enter();
    FlacEvent(context,AudioEventKind::FlacBegin);
    NativeTimer timing(native_init_timing);
    auto native_result=original_flac_init(context,result);timing.Finish();
    return native_result;
}
static int FlacReadHook(void* context,unsigned char* buffer,size_t* length) {
    auto callback=audio_callbacks.Enter();
    size_t requested=*length;
    NativeTimer timing(native_read_timing);
    int status=original_flac_read(context,buffer,length);timing.Finish();
    if(status==0 && !*length && HookDiagnosticsEnabled())empty_flac_reads.fetch_add(1,std::memory_order_relaxed);
    if(status==0 && *length && *length<=requested) FlacEvent(context,AudioEventKind::FlacBytes,buffer,*length);
    else if(status==1 && !*length) FlacEvent(context,AudioEventKind::FlacEnd);
    else if(status!=0 || *length>requested) FlacEvent(context,AudioEventKind::FlacError,nullptr,0,unsigned(status),FlacErrorSource::ReadCallback,requested,*length);
    return status;
}
static int FlacFrameHook(void* context,const void* frame,const void* pcm) {
    auto callback=audio_callbacks.Enter();
    NativeTimer timing(native_frame_timing);
    int status=original_flac_frame(context,frame,pcm);timing.Finish();
    if(status==0) FlacEvent(context,AudioEventKind::FlacFrame,frame,32);
    else FlacEvent(context,AudioEventKind::FlacError,nullptr,0,unsigned(status),FlacErrorSource::FrameCallback);
    return status;
}
static void FlacErrorHook(void* context,unsigned error) {
    auto callback=audio_callbacks.Enter();
    NativeTimer timing(native_error_timing);
    original_flac_error(context,error);timing.Finish();
    FlacEvent(context,AudioEventKind::FlacError,nullptr,0,error,FlacErrorSource::DecoderCallback);
}
static uint64_t SaturatingAdd(uint64_t left,uint64_t right) noexcept {
    return left>std::numeric_limits<uint64_t>::max()-right?
        std::numeric_limits<uint64_t>::max():left+right;
}
static void QueueDecodedActivity(uintptr_t context,size_t produced,size_t consumed,size_t kept,
                                 double time,unsigned epoch) noexcept {
    AcquireSRWLockExclusive(&queue_lock);
    const auto sequence=++producer_sequence;
    if(decoded_activity_count) {
        const auto previous=(decoded_activity_tail+decoded_activity_capacity-1)%decoded_activity_capacity;
        auto& slot=decoded_activity_queue[previous];
        if(slot.context==context&&slot.epoch==epoch&&slot.last_sequence+1==sequence) {
            slot.calls=SaturatingAdd(slot.calls,1);
            slot.produced_floats=SaturatingAdd(slot.produced_floats,produced);
            slot.consumed_bytes=SaturatingAdd(slot.consumed_bytes,consumed);
            slot.kept_floats=SaturatingAdd(slot.kept_floats,kept);
            slot.last=time;slot.last_sequence=sequence;
            ReleaseSRWLockExclusive(&queue_lock);
            if(event)SetEvent(event);
            return;
        }
    }
    if(decoded_activity_count==decoded_activity_capacity) {
        InterlockedIncrement(&decoded_activity_dropped);
    } else {
        auto& slot=decoded_activity_queue[decoded_activity_tail];
        slot={context,1,produced,consumed,kept,time,time,epoch,sequence,sequence};
        decoded_activity_tail=(decoded_activity_tail+1)%decoded_activity_capacity;
        ++decoded_activity_count;
    }
    ReleaseSRWLockExclusive(&queue_lock);
    if(event)SetEvent(event);
}
static bool PopDecodedActivity(DecodedActivitySlot& slot) noexcept {
    AcquireSRWLockExclusive(&queue_lock);
    const bool available=decoded_activity_count!=0;
    if(available) {
        slot=decoded_activity_queue[decoded_activity_head];
        decoded_activity_queue[decoded_activity_head]={};
        decoded_activity_head=(decoded_activity_head+1)%decoded_activity_capacity;
        --decoded_activity_count;
    }
    ReleaseSRWLockExclusive(&queue_lock);
    return available;
}
static void ResetDecodedActivityQueue() noexcept {
    AcquireSRWLockExclusive(&queue_lock);
    decoded_activity_queue={};decoded_activity_head=decoded_activity_tail=decoded_activity_count=0;
    InterlockedExchange(&decoded_activity_dropped,0);decoded_activity_loss_seen=0;
    ReleaseSRWLockExclusive(&queue_lock);
    ResetMp3Probes();
}
static bool FirstOwnershipOutput(uint64_t owner) noexcept {
    if(!owner)return false;
    for(unsigned index=0;index<ownership_output_logged_count;++index)
        if(ownership_output_logged[index]==owner)return false;
    if(ownership_output_logged_count<ownership_output_logged.size()) {
        ownership_output_logged[ownership_output_logged_count++]=owner;return true;
    }
    ownership_output_logged[ownership_output_logged_next]=owner;
    ownership_output_logged_next=(ownership_output_logged_next+1)%ownership_output_logged.size();
    return true;
}
static void DrainDecodedActivity(ownership::DecoderOwnershipTracker& tracker,unsigned epoch) {
    const auto losses=InterlockedCompareExchange(&decoded_activity_dropped,0,0);
    if(losses!=decoded_activity_loss_seen) {
        decoded_activity_loss_seen=losses;
        tracker.ObservationLost(ownership::ObservationLoss::DecodedQueue,Now());
        if(DebugLoggingEnabled())Log(LogKind::Diagnostic,"ownership event=loss source=decoded-queue");
    }
    DecodedActivitySlot slot{};
    while(PopDecodedActivity(slot)) {
        if(slot.epoch!=epoch) {
            tracker.ObservationLost(ownership::ObservationLoss::Epoch,slot.last);continue;
        }
        uintptr_t owner=0;
        if(!ownership::NormalizeDecoderOwner(slot.context,0,owner)) {
            tracker.ObservationLost(ownership::ObservationLoss::DecodedQueue,slot.last);continue;
        }
        ownership::DecodedActivity activity{};activity.owner=owner;activity.calls=slot.calls;
        activity.produced_floats=slot.produced_floats;activity.consumed_bytes=slot.consumed_bytes;
        activity.kept_floats=slot.kept_floats;activity.first=slot.first;activity.last=slot.last;
        activity.epoch=slot.epoch;activity.sequence=slot.first_sequence;tracker.ObserveDecoded(activity);
        const auto token=tracker.OwnerToken(owner);
        if(slot.produced_floats&&DebugLoggingEnabled()&&FirstOwnershipOutput(token)) {
            char line[260];snprintf(line,sizeof(line),
                "ownership event=output-first owner=%llu calls=%llu produced=%llu consumed=%llu kept=%llu mono=%.3f",
                static_cast<unsigned long long>(token),static_cast<unsigned long long>(slot.calls),
                static_cast<unsigned long long>(slot.produced_floats),
                static_cast<unsigned long long>(slot.consumed_bytes),
                static_cast<unsigned long long>(slot.kept_floats),slot.first);
            Log(LogKind::Diagnostic,line);
        }
    }
}
static bool AdvancedSpan(const decoded_output::Span& before,const decoded_output::Span& after,
                         size_t element,size_t& consumed) noexcept {
    consumed=0;
    if(after.size>before.size||(!before.data&&before.size)||(!after.data&&after.size))return false;
    consumed=before.size-after.size;
    if(consumed>std::numeric_limits<size_t>::max()/element)return false;
    const auto address=reinterpret_cast<uintptr_t>(before.data);
    const auto bytes=consumed*element;
    return address<=std::numeric_limits<uintptr_t>::max()-bytes &&
           reinterpret_cast<uintptr_t>(after.data)==address+bytes;
}
static void RequestDecodedFallback(DecodedFallbackReason reason) noexcept {
    unsigned empty=unsigned(DecodedFallbackReason::None);
    decoded_fallback_reason.compare_exchange_strong(
        empty,unsigned(reason),std::memory_order_release,std::memory_order_relaxed);
    decoded_fallback_requested.store(true,std::memory_order_release);
}
static void* DecodedSinkHook(void* context,void* result,decoded_output::Span* decoded,
                             decoded_output::Span* encoded,bool mode) {
    auto callback=audio_callbacks.Enter();
    const auto decoded_before=decoded?*decoded:decoded_output::Span{};
    const auto encoded_before=encoded?*encoded:decoded_output::Span{};
    auto* native_result=original_decoded_process(context,result,decoded,encoded,mode);
    const auto native_error=GetLastError();
    const bool was_enabled=decoded_accelerator.Enabled();
    const auto speed=CaptureSpeed();size_t produced=0,consumed=0,kept=0;
    DecodedFallbackReason failure=DecodedFallbackReason::None;
    bool decoded_valid=false,encoded_valid=false;
    if(!decoded||!encoded)failure=DecodedFallbackReason::MissingSpan;
    else {
        decoded_valid=AdvancedSpan(decoded_before,*decoded,sizeof(float),produced);
        encoded_valid=AdvancedSpan(encoded_before,*encoded,1,consumed);
        if(!decoded_valid||produced>max_decoded_callback_floats)failure=DecodedFallbackReason::DecodedSpan;
        else if(!encoded_valid||consumed>max_decoded_callback_bytes)failure=DecodedFallbackReason::EncodedSpan;
    }
    const bool valid=failure==DecodedFallbackReason::None;
    if(!valid) {
        decoded_accelerator.Disable();InterlockedIncrement(&decoded_activity_dropped);
        if(event)SetEvent(event);
    } else {
        kept=produced;
        if(speed>1&&produced) {
            const auto keep=decoded_accelerator.Keep(context,produced,speed);
            decoded->data=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(decoded_before.data)+keep*sizeof(float));
            decoded->size=decoded_before.size-keep;
            kept=keep;
        }
        if(was_enabled&&!decoded_accelerator.Enabled()) {
            failure=!context?DecodedFallbackReason::MissingOwner:
                !capture_speed::Supported(speed)?DecodedFallbackReason::UnsupportedSpeed:
                DecodedFallbackReason::OddSamples;
        }
        const auto time=Now();const auto epoch=CaptureEpoch();
        if(PodcastsEnabled()&&consumed)
            QueueMp3Encoded(reinterpret_cast<uintptr_t>(context),static_cast<const unsigned char*>(encoded_before.data),consumed,time,epoch);
        QueueDecodedActivity(reinterpret_cast<uintptr_t>(context),produced,consumed,kept,time,epoch);
    }
    if(was_enabled&&!decoded_accelerator.Enabled())RequestDecodedFallback(failure);
    SetLastError(native_error);return native_result;
}
static int PlaybackPumpHook(void* owner,void* second,void* third) {
    auto callback=audio_callbacks.Enter();
    if(owner&&playback_pump_decoder_offset>0) {
        uintptr_t decoder=0;
        std::memcpy(&decoder,static_cast<const unsigned char*>(owner)+playback_pump_decoder_offset,
                    sizeof(decoder));
        const playback_identity::PlayerPath path{0x78,0x1e8,0x9b8};
        const auto read=[](const void* source,void* destination,size_t size) noexcept {
            SIZE_T copied=0;
            return ReadProcessMemory(GetCurrentProcess(),source,destination,size,&copied)&&copied==size;
        };
        playback_identity::Instance instance{};
        if(decoder&&playback_identity::ReadPlaybackInstance(owner,path,read,instance)&&
           TryAcquireSRWLockExclusive(&playback_identity_lock)) {
            const auto epoch=CaptureEpoch();
            if(playback_identity_bridge.NeedsObservation(
                   reinterpret_cast<uintptr_t>(owner),decoder,epoch,
                   playback_identity::SnapshotRole::PlayerPlaybackId,
                   instance.bytes.data(),instance.bytes.size())) {
                const auto sequence=playback_identity_sequence.fetch_add(1,std::memory_order_relaxed)+1;
                playback_identity_bridge.Observe(reinterpret_cast<uintptr_t>(owner),decoder,epoch,
                    sequence,instance.bytes.data(),instance.bytes.size(),
                    playback_identity::SnapshotRole::PlayerPlaybackId);
                if(event)SetEvent(event);
            }
            ReleaseSRWLockExclusive(&playback_identity_lock);
        }
    }
    return original_playback_pump(owner,second,third);
}
static void LogPlaybackIdentityMatch() {
    if(!playback_pump_target||!DebugLoggingEnabled())return;
    std::optional<playback_identity::ObservationInfo> observation;
    bool lost=false;
    AcquireSRWLockExclusive(&playback_identity_lock);
    observation=playback_identity_bridge.TakeObservation();
    lost=playback_identity_bridge.ObservationLost();
    ReleaseSRWLockExclusive(&playback_identity_lock);
    if(observation) {
        const char* role=observation->role==playback_identity::SnapshotRole::Owner?"owner":
            observation->role==playback_identity::SnapshotRole::Decoder?"decoder":"player-id";
        char line[200];snprintf(line,sizeof(line),
            "identity-bridge event=snapshot role=%s owner_generation=%llu epoch=%u sequence=%llu",
            role,
            static_cast<unsigned long long>(observation->owner_generation),observation->epoch,
            static_cast<unsigned long long>(observation->sequence));
        Log(LogKind::Diagnostic,line);
    }
    static bool loss_reported=false;
    if(lost&&!loss_reported) {
        loss_reported=true;
        Log(LogKind::Diagnostic,"identity-bridge event=loss; direct identity disabled for affected evidence");
    }
}
static size_t SetExpectedPlaybackIdentity(
    const Media& media,std::uint64_t generation,
    std::array<playback_identity::Match,playback_identity::kObservationCapacity>& matches) {
    if(!playback_pump_target||!DebugLoggingEnabled()||media.playback_instance.empty()||!generation)return 0;
    const bool changed=playback_identity_metadata_generation!=generation;
    AcquireSRWLockExclusive(&playback_identity_lock);
    const bool accepted=!changed||playback_identity_bridge.SetExpected(media.playback_instance,generation);
    size_t count=0;
    while(count<matches.size()) {
        const auto match=playback_identity_bridge.TakeMatch();
        if(!match)break;
        matches[count++]=*match;
    }
    ReleaseSRWLockExclusive(&playback_identity_lock);
    if(!accepted)return 0;
    if(changed) {
        playback_identity_metadata_generation=generation;
        char line[160];snprintf(line,sizeof(line),
            "identity-bridge event=metadata metadata_generation=%llu normalized=1",
            static_cast<unsigned long long>(generation));
        Log(LogKind::Diagnostic,line);
    }
    for(size_t index=0;index<count;++index) {
        const auto& match=matches[index];
        const char* role=match.role==playback_identity::SnapshotRole::Owner?"owner":
            match.role==playback_identity::SnapshotRole::Decoder?"decoder":"player-id";
        char line[280];snprintf(line,sizeof(line),
            "identity-bridge event=match role=%s metadata_generation=%llu owner_generation=%llu epoch=%u sequence=%llu representation=%s offset=0x%zx",
            role,static_cast<unsigned long long>(match.metadata_generation),
            static_cast<unsigned long long>(match.owner_generation),match.epoch,
            static_cast<unsigned long long>(match.sequence),
            match.representation==playback_identity::Representation::RawBytes?"raw":"ascii-hex",
            match.offset);
        Log(LogKind::Diagnostic,line);
    }
    return count;
}
static void QueueAac(void* context,AudioEventKind kind,const unsigned char* bytes,size_t size,
                     double time,unsigned epoch,unsigned status=0) {
    {
        QueueGuard guard;
        if(count==capacity)InterlockedIncrement(&dropped);
        else {
            auto& s=queue[tail];s.context=reinterpret_cast<uintptr_t>(context);s.kind=kind;
            s.capture=true;s.epoch=epoch;s.time=time;s.length=unsigned(size);s.format=status;
            s.sequence=++producer_sequence;
            if(size)memcpy(s.bytes,bytes,size);
            tail=(tail+1)%capacity;++count;
        }
    }
    SetEvent(event);
}
static void* AacInitializeHook(void* context,bool mode) {
    auto callback=audio_callbacks.Enter();
    auto result=original_aac_initialize(context,mode);
    if(AACEnabled())QueueAac(context,AudioEventKind::AacBegin,nullptr,0,Now(),CaptureEpoch());
    return result;
}
static void* AacPushHook(void* context,void* status,const AacView* input) {
    auto callback=audio_callbacks.Enter();
    if(!AACEnabled())return original_aac_push(context,status,input);
    const auto epoch=CaptureEpoch();const auto time=Now();
    // Original input is borrowed and may be released by the demuxer. Copy it
    // before calling through; enqueue only after checking the original status.
    unsigned char bytes[65307];
    const auto size=input?input->size:0;
    const bool bounded=input&&input->bytes&&size<=sizeof(bytes);
    if(bounded&&size)memcpy(bytes,input->bytes,size);
    auto result=original_aac_push(context,status,input);
    uint32_t code=~0u;if(status)memcpy(&code,status,sizeof(code));
    if(!bounded||code>1)QueueAac(context,AudioEventKind::AacError,nullptr,0,time,epoch,code);
    else if(size)QueueAac(context,AudioEventKind::AacBytes,bytes,size,time,epoch);
    return result;
}
static void* AacDestroyHook(void* context,unsigned flags) {
    auto callback=audio_callbacks.Enter();
    if(AACEnabled())QueueAac(context,AudioEventKind::AacDestroy,nullptr,0,Now(),CaptureEpoch());
    return original_aac_destroy(context,flags);
}
struct Capture {
    Stream stream;
    bool lossless=false,metadata=false,aac=false,mp3=false,unverified_reported=false,ambiguous=false;
    mutable int final_validation=-1;
    mutable unsigned validation_runs=0;
    bool publication_deferred=false;
    SpacStream spac;
    Mp3Info mp3_info;
    FlacInfo flac;
    FlacCoverage coverage;
    size_t flac_audio_offset=0;
    double born=0,finished=0;
    uint64_t bytes=0,listen_generation=0;
    double last_input=0;
    unsigned capture_epoch=0;
    uintptr_t capture_context=0;
    uintptr_t decoder_owner=0;
    uint64_t capture_token=0;
    uint64_t reported_playback_token=0;
    uint64_t reported_generation=0;
    unsigned reported_ownership_state=~0u;
    unsigned reported_ownership_reason=~0u;
    std::string bound_identity;
    CompressedBuffer data;
    size_t Capacity() const {return data.capacity()+(aac?spac.MemoryUsage():0);}
    double Duration() const {
        if(mp3)return mp3_info.Duration();
        if(aac)return spac.Duration();
        return lossless&&flac.rate?double(coverage.samples)/flac.rate:stream.Duration();
    }
    Quality Encoding() const {
        if(mp3)return {Codec::Mp3,mp3_info.rate,mp3_info.channels,0,
            mp3_info.Duration()>0?unsigned((mp3_info.audio_size*8)/mp3_info.Duration()):0};
        if(aac)return {Codec::Aac,spac.info.rate,spac.info.channels,0,0};
        if(lossless)return {Codec::Flac,flac.rate,flac.channels,flac.bits,0};
        return {Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate};
    }
    uint64_t Samples() const {
        if(mp3)return mp3_info.samples;
        if(aac)return spac.info.total_samples;
        return lossless?coverage.samples:uint64_t(stream.samples);
    }
    uint64_t SourceUnits() const {
        if(mp3)return mp3_info.frames;
        if(aac) {
            const auto samples=uint64_t(spac.info.total_samples)+spac.info.priming;
            return spac.info.samples_per_packet?(samples+spac.info.samples_per_packet-1)/spac.info.samples_per_packet:0;
        }
        return lossless?coverage.frames:stream.pages;
    }
    const wchar_t* Extension() const {return mp3?L".mp3":aac?L".m4a":lossless?L".flac":L".ogg";}
    const char* Source() const {
        if(mp3)return "native compressed MP3 input";
        if(aac)return "native compressed SpAC AAC packets";
        return lossless?"native compressed FLAC input":"native compressed Ogg pages";
    }
};
enum class FinalCaptureValidation { Valid,Incomplete,Invalid };
static FinalCaptureValidation ValidateOggCapture(const Capture& capture) {
    Stream rebuilt;size_t at=0;std::vector<uint8_t> page;page.reserve(65307);
    while(at<capture.data.size()) {
        const auto remaining=capture.data.size()-at;
        if(remaining<27)return FinalCaptureValidation::Invalid;
        const size_t header=27+capture.data[at+26];
        if(header>remaining)return FinalCaptureValidation::Invalid;
        size_t body=0;
        for(size_t i=at+27;i<at+header;++i)body+=capture.data[i];
        if(body>remaining-header)return FinalCaptureValidation::Invalid;
        const size_t length=header+body;page.resize(length);
        for(size_t i=0;i<length;++i)page[i]=capture.data[at+i];
        const auto result=rebuilt.Push(page.data(),page.size());at+=length;
        if(result==Result::Invalid||result==Result::Ignore)return FinalCaptureValidation::Invalid;
        if(result==Result::Complete&&at!=capture.data.size())return FinalCaptureValidation::Invalid;
    }
    if(!rebuilt.complete)return FinalCaptureValidation::Incomplete;
    if(!capture.stream.complete||rebuilt.serial!=capture.stream.serial||rebuilt.rate!=capture.stream.rate||
       rebuilt.channels!=capture.stream.channels||rebuilt.pages!=capture.stream.pages||
       rebuilt.samples!=capture.stream.samples)return FinalCaptureValidation::Invalid;
    return FinalCaptureValidation::Valid;
}
static FinalCaptureValidation ValidateAacCapture(const Capture& capture) {
    SpacStream rebuilt;std::vector<uint8_t> chunk(65536);size_t at=0;
    while(at<capture.data.size()) {
        const size_t amount=std::min(chunk.size(),capture.data.size()-at);
        for(size_t i=0;i<amount;++i)chunk[i]=capture.data[at+i];
        const auto result=rebuilt.Push(chunk.data(),amount);
        if(result==AacParse::Invalid||result==AacParse::Unsupported)return FinalCaptureValidation::Invalid;
        at+=amount;
    }
    if(!rebuilt.Complete())return FinalCaptureValidation::Incomplete;
    const auto& expected=capture.spac.info;const auto& actual=rebuilt.info;
    if(!capture.spac.Complete()||actual.rate!=expected.rate||actual.channels!=expected.channels||
       actual.samples_per_packet!=expected.samples_per_packet||actual.priming!=expected.priming||
       actual.total_samples!=expected.total_samples||actual.config!=expected.config)
        return FinalCaptureValidation::Invalid;
    return FinalCaptureValidation::Valid;
}
static FinalCaptureValidation ValidateFinalCapture(const Capture& capture) {
    if(capture.bytes!=capture.data.size())return FinalCaptureValidation::Invalid;
    if(capture.mp3) {
        Mp3Info parsed;std::string error;
        if(!InspectMp3Source(capture.data,parsed,error))return FinalCaptureValidation::Invalid;
        return parsed.rate==capture.mp3_info.rate&&parsed.channels==capture.mp3_info.channels&&
            parsed.frames==capture.mp3_info.frames&&parsed.samples==capture.mp3_info.samples&&
            parsed.audio_offset==capture.mp3_info.audio_offset&&parsed.audio_size==capture.mp3_info.audio_size?
            FinalCaptureValidation::Valid:FinalCaptureValidation::Invalid;
    }
    if(capture.lossless) {
        FlacInfo parsed;size_t audio=0;
        if(ParseFlacMetadata(capture.data,parsed,audio)!=FlacParse::Valid || audio>=capture.data.size())
            return FinalCaptureValidation::Invalid;
        if(parsed.rate!=capture.flac.rate||parsed.channels!=capture.flac.channels||
           parsed.bits!=capture.flac.bits||parsed.min_block!=capture.flac.min_block||
           parsed.max_block!=capture.flac.max_block||parsed.total_samples!=capture.flac.total_samples)
            return FinalCaptureValidation::Invalid;
        return capture.metadata&&capture.coverage.Complete(capture.flac)?
            FinalCaptureValidation::Valid:FinalCaptureValidation::Incomplete;
    }
    if(capture.aac)return ValidateAacCapture(capture);
    return ValidateOggCapture(capture);
}
static bool ValidateFlattenedCapture(const Capture& capture,const std::vector<uint8_t>& source) {
    if(capture.aac||capture.mp3)return true; // These streams were reparsed immediately before Flatten().
    RecordingFile recording;
    if(!InspectRecording(source.data(),source.size(),recording)||!recording.complete)return false;
    const auto codec=capture.lossless?Codec::Flac:Codec::Vorbis;
    if(recording.quality.codec!=codec||recording.quality.rate!=(capture.lossless?capture.flac.rate:capture.stream.rate)||
       recording.quality.channels!=(capture.lossless?capture.flac.channels:capture.stream.channels))return false;
    if(capture.lossless&&recording.quality.bits!=capture.flac.bits)return false;
    return true;
}
static FinalCaptureValidation CachedFinalValidation(const Capture& capture) {
    if(capture.final_validation<0) {
        ++capture.validation_runs;
        capture.final_validation=int(ValidateFinalCapture(capture));
    }
    return FinalCaptureValidation(capture.final_validation);
}
// Called only when ownership reconciliation has finished and this object really
// will be released. Flatten keeps the source alive through the final whole-file
// inspection and avoids retaining another permanent copy.
static const char* CheckBeforeRelease(Capture& capture,const char* reason,
                                     const ownership::DecoderOwnershipTracker* tracker=nullptr) {
    const char* result="validation unavailable";
    try {
        const bool guarded_complete=capture.mp3?capture.mp3_info.frames!=0:capture.lossless?(capture.metadata&&capture.coverage.Complete(capture.flac)):
            capture.aac?capture.spac.Complete():capture.stream.complete;
        const bool verified=guarded_complete&&CachedFinalValidation(capture)==FinalCaptureValidation::Valid;
        // Inspect the bytes independently: a failed decoder/coverage flag does
        // not establish that the complete compressed source was lost.
        auto source=capture.data.Flatten();bool complete=false;
        if(capture.aac) {
            SpacStream rebuilt;rebuilt.Push(source.data(),source.size());complete=rebuilt.Complete();
        }else if(capture.mp3) {
            Mp3Info info;std::string error;complete=InspectMp3Source(source.data(),source.size(),info,error);
        }else {
            RecordingFile recording;
            complete=InspectRecording(source.data(),source.size(),recording)&&recording.complete;
        }
        result=complete?
            (capture.ambiguous?"complete valid audio; ownership ambiguous":!verified?
                "complete valid audio; capture/listen verification failed":capture.publication_deferred?
                "complete valid audio; publication deferred":"complete valid audio; no verified metadata/listen match"):
            guarded_complete?"invalid cached audio":"incomplete or invalid cached audio";
    } catch(...) { /* Release under the existing memory bound, with an honest outcome. */ }
    const auto binding=tracker&&capture.capture_token?tracker->Binding(capture.capture_token):
        ownership::OwnershipBinding{capture.capture_token,0,0,0,ownership::OwnershipState::Invalidated,
                                    ownership::OwnershipReason::MissingCapture};
    char line[420];snprintf(line,sizeof(line),
        "final cache check: %s; release reason=%s; bytes=%llu; ownership_state=%u ownership_reason=%s capture=%llu",
        result,reason,static_cast<unsigned long long>(capture.bytes),unsigned(binding.state),
        ownership::OwnershipReasonName(binding.reason),
        static_cast<unsigned long long>(capture.capture_token));
    Log(LogKind::Diagnostic,line);
    const char* outcome=std::strcmp(result,"incomplete or invalid cached audio")==0?"incomplete cache":
        std::strcmp(result,"invalid cached audio")==0?"invalid cache":
        std::strstr(result,"ownership ambiguous")?"ambiguous cache":
        std::strstr(result,"publication deferred")?"deferred cache":
        std::strstr(result,"no verified metadata/listen match")?"unmatched cache":
        std::strstr(result,"verification failed")?"unverified cache":"cache validation failed";
    LogEvent(LogKind::Failure,"failed",std::string(outcome)+" | "+
        (capture.bound_identity.empty()?"unknown track":capture.bound_identity));
    return result;
}
static bool FormatEnabled(const Capture& capture,const Settings& settings) {
    return capture.mp3?settings.podcasts:capture.aac?settings.aac:capture.lossless?settings.flac:settings.ogg;
}
using Heard=PlaybackListen;
static double DeclaredCaptureDuration(const Capture& capture) {
    if(capture.mp3&&capture.mp3_info.rate)return capture.mp3_info.Duration();
    if(capture.lossless&&capture.metadata&&capture.flac.rate&&capture.flac.total_samples)
        return double(capture.flac.total_samples)/capture.flac.rate;
    if(capture.aac&&capture.spac.info.rate&&capture.spac.info.total_samples)
        return double(capture.spac.info.total_samples)/capture.spac.info.rate;
    if(capture.stream.complete)return capture.Duration();
    return 0;
}
static bool CaptureDurationCouldMatch(const Capture& capture,const Heard& heard) {
    const auto declared=DeclaredCaptureDuration(capture);
    return !declared||std::fabs(declared-heard.media.duration)<=1.0;
}
static bool CaptureTimingMatchesListen(const Capture& capture,const Heard& heard) {
    if(capture.listen_generation)return capture.listen_generation==heard.generation;
    // Capability fallback only: without a decoded owner/listen binding, timing
    // can keep a candidate viable but can never resolve competing candidates.
    return capture.born>=heard.start-20&&capture.born<=heard.start+3&&capture.born<heard.finish;
}
static Catalog CatalogFor(const Media& media) {
    Catalog c; c.spotify_uri=media.spotify_uri; c.title=media.title; c.album=media.album; c.track=media.track;c.duration=media.duration;
    c.artist=media.album_artist.empty() ? media.artist : media.album_artist;
    c.folder_artist=media.album_artist;
    if(media.spotify_uri.rfind("spotify:episode:",0)==0) {
        c.kind=MediaKind::Podcast;c.show=media.album;
    } else if(_wcsicmp(c.artist.c_str(),L"Various Artists")==0) c.kind=MediaKind::VariousArtists;
    EnrichCatalog(media,c);return c;
}
static void QueueFilenameCheck(const Media&,const Settings&);
static void CancelFilenameChecks();
static void PublishCurrentDestination(const Media& media,const Settings& settings) {
    PublishSongDestination(media.title.empty() ? 0 : PlaybackIdentity(media.Key()),
                           settings.root,CatalogFor(media),settings.music_folder,settings.generation,settings.track_number_in_filename);
    QueueFilenameCheck(media,settings);
}
static std::string Grouped(unsigned value) {
    auto text=std::to_string(value);
    for(int p=int(text.size())-3;p>0;p-=3)text.insert(size_t(p),",");
    return text;
}
static std::string SavedAudioDetails(const Quality& quality,size_t bytes) {
    std::string text=quality.codec==Codec::Flac ? "FLAC" : quality.codec==Codec::Aac?"AAC (M4A)":quality.codec==Codec::Mp3?"MP3":"Ogg Vorbis";
    if(quality.codec==Codec::Flac && quality.bits)text+=" "+std::to_string(quality.bits)+"-bit";
    if(quality.codec==Codec::Vorbis && quality.bitrate) {
        text+=quality.bitrate%1000 ? " "+std::to_string(quality.bitrate)+" bps nominal" : " "+std::to_string(quality.bitrate/1000)+" kbps nominal";
    }
    if(quality.rate)text+=", "+Grouped(quality.rate)+" Hz";
    char size[96];snprintf(size,sizeof(size),"%.2f MiB (%llu bytes)",double(bytes)/(1024*1024),static_cast<unsigned long long>(bytes));
    return text+" | "+size;
}
static std::string ExistingSkipReason(const std::wstring& path,const Quality& incoming,
                                      const Catalog& catalog,const RecordingFile* inspected=nullptr) {
    RecordingFile fresh;
    if(!inspected) {
        if(!ReadRecording(path,fresh))return "existing file quality could not be verified; retained";
        inspected=&fresh;
    }
    if(!inspected->complete)return "existing file quality could not be verified; retained";
    if(!MatchesRecording(*inspected,catalog))return "different recording found at destination; retained";
    if(!inspected->owned)return "existing file is not a Floggfy recording; retained";
    const auto& existing=inspected->quality;
    const bool same=existing.codec==incoming.codec && existing.rate && existing.rate==incoming.rate &&
        existing.channels && existing.channels==incoming.channels &&
        (existing.codec==Codec::Flac ? existing.bits && existing.bits==incoming.bits : existing.bitrate && existing.bitrate==incoming.bitrate);
    if(same)return "same quality already exists on disk";
    if(HigherQuality(existing,incoming))return "better quality already exists on disk";
    return "existing file retained; incoming audio is not a verified quality upgrade";
}
static void SetIdentityTag(Tags& tags,const char* name,const std::string& value) {
    tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& field){return field.first==name;}),tags.fields.end());
    if(!value.empty()) tags.fields.emplace_back(name,value);
}
static void ApplyCatalogIdentity(Tags& tags,const Catalog& catalog) {
    SetIdentityTag(tags,"TITLE",Utf8(catalog.title));SetIdentityTag(tags,"ALBUM",Utf8(catalog.album));
    SetIdentityTag(tags,"ALBUMARTIST",Utf8(catalog.artist));
    SetIdentityTag(tags,"TRACKNUMBER",catalog.track ? std::to_string(catalog.track) : std::string{});
    SetIdentityTag(tags,"DISCNUMBER",catalog.disc ? std::to_string(catalog.disc) : std::string{});
    SetIdentityTag(tags,"SPOTIFY_URI",catalog.spotify_uri);
}
static std::atomic<uint64_t> filename_check_serial{0};
struct FilenamePermit {unsigned generation=0;uint64_t serial=0;};
static bool FilenameStillCurrent(void* argument) {
    const auto& permit=*static_cast<FilenamePermit*>(argument);
    const auto current=GetSettings();
    return current.generation==permit.generation && current.downloads &&
        current.convert_numbered_filenames && !current.track_number_in_filename &&
        (!permit.serial || permit.serial==filename_check_serial.load(std::memory_order_acquire));
}
// Shared by track-start checking and the completed-listen fallback. Only the
// publication worker invokes this helper; menu/capture code never renames files.
static FilenameConversion ConvertResolvedFilename(RecordingDestination& resolved,const Catalog& catalog,
    const Settings& settings,const wchar_t* extension,uint64_t serial=0) {
    if(!settings.convert_numbered_filenames || settings.track_number_in_filename ||
       resolved.state!=RecordingDestinationState::Existing || !resolved.recording.complete || !resolved.recording.owned)
        return FilenameConversion::Unchanged;
    const auto latest=GetSettings();
    if(latest.generation!=settings.generation || !latest.downloads || !latest.convert_numbered_filenames || latest.track_number_in_filename)
        return FilenameConversion::Unchanged;
    const auto expected=OutputPath(settings.root,catalog,extension,settings.music_folder,false);
    // Reused recordings keep their existing folder, including the recognized
    // Spotify-artist alias. Conversion removes only the filename prefix.
    const auto title_path=resolved.path.substr(0,resolved.path.find_last_of(L"\\/")+1)+
        expected.substr(expected.find_last_of(L"\\/")+1);
    std::wstring renamed;
    FilenamePermit permit{settings.generation,serial};
    const auto result=ConvertRecordingFilename(resolved.path,title_path,catalog,renamed,FilenameStillCurrent,&permit);
    if(result==FilenameConversion::Renamed) {
        resolved.path.swap(renamed); // renamed now holds the previous path.
        RelocateRecentDownload(renamed,resolved.path);
        RefreshSongDestination();
        LogEvent(LogKind::Activity,"renamed","removed filename number prefix; existing audio and metadata retained | "+Utf8(catalog.title));
    }
    return result;
}
enum class Publication { Saved,Skipped,Failed };
static LogKind PublicationLogKind(Publication status,bool newly_saved) {
    if(status==Publication::Failed)return LogKind::Failure;
    if(status==Publication::Saved&&newly_saved)return LogKind::Finished;
    return LogKind::Activity;
}
static Publication Publish(Capture& c,const Heard& heard,unsigned expected_epoch) {
    const std::string identity=Utf8(heard.media.artist)+" - "+Utf8(heard.media.title);
    auto finish=[&](Publication status,const char* event,const std::string& detail,bool newly_saved=false) {
        // Put outcome facts first so a long title cannot truncate quality/size.
        LogEvent(PublicationLogKind(status,newly_saved),event,detail+" | "+identity);return status;
    };
    SYSTEMTIME t; GetSystemTime(&t); wchar_t stamp[64];
    swprintf(stamp,64,L"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    auto catalog=CatalogFor(heard.media);
    auto settings=GetSettings();
    if(settings.capture_epoch!=expected_epoch || !settings.downloads || !FormatEnabled(c,settings)) return finish(Publication::Skipped,"skipped","capture settings changed or downloads/format disabled");
    Quality quality=c.Encoding();
    Tags tags;
    tags.fields={{"TITLE",Utf8(heard.media.title)},{"ARTIST",Utf8(heard.media.artist)},
      {"ALBUM",Utf8(heard.media.album)},{"ALBUMARTIST",Utf8(heard.media.album_artist)},
      {"TRACKNUMBER",std::to_string(heard.media.track)},
      {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_TRANSCODED","0"},
      {"HISTORY_PLAYBACK_SOURCE",heard.media.source==MediaSource::Spotify?"spotify":"windows"},
      {"HISTORY_SAMPLES",std::to_string(c.Samples())},
      {"HISTORY_SAMPLE_RATE",std::to_string(quality.rate)},
      {"HISTORY_BITS_PER_SAMPLE",std::to_string(quality.bits)},
      {"HISTORY_NOMINAL_BITRATE",std::to_string(quality.bitrate)},
      {"HISTORY_MEDIA_DURATION",std::to_string(heard.media.duration)},
      {"HISTORY_SOURCE_PAGES",std::to_string(c.SourceUnits())},
      {"HISTORY_SOURCE_BYTES",std::to_string(c.bytes)},
      {"HISTORY_ELAPSED_SECONDS",std::to_string(heard.finish-heard.start)},
      {"HISTORY_SAVED_UTC",Utf8(stamp)},{"HISTORY_CAPTURE",c.Source()}};
    if(!heard.media.genre.empty())tags.fields.push_back({"GENRE",Utf8(heard.media.genre)});
    EnrichTags(heard.media,tags);
    // Use the same RAM-only canonical catalog as Song probing. Enrichment may
    // arrive while a completed listen waits for publication; make final tags
    // and path planning agree before inspecting any existing recording.
    catalog=CatalogFor(heard.media);ApplyCatalogIdentity(tags,catalog);
    const auto capture_path=OutputPath(settings.root,catalog,c.Extension(),settings.music_folder,settings.track_number_in_filename);
    RecordingDirectory directory;
    if(!ListRecordingDirectory(capture_path,directory,&catalog))
        return finish(Publication::Failed,"failed","recording directory could not be listed; publication deferred");
    RecordingDestination superseded_vorbis;
    bool format_upgrade=false;
    if(c.lossless && !c.mp3) {
        auto ogg=ResolveRecordingDestination(
            OutputPath(settings.root,catalog,L".ogg",settings.music_folder,settings.track_number_in_filename),
            catalog,settings.track_number_in_filename,&directory);
        if(ogg.state==RecordingDestinationState::Blocked)
            return finish(Publication::Failed,"failed","existing Ogg could not be inspected; publication deferred");
        if(ogg.state==RecordingDestinationState::Existing && ogg.recording.complete && ogg.recording.owned &&
           ogg.recording.quality.codec==Codec::Vorbis && HigherQuality(quality,ogg.recording.quality)) {
            superseded_vorbis=std::move(ogg);
            format_upgrade=true;
        }
    }
    // Search preferred formats first. Blocked is I/O uncertainty, never proof
    // that a recording is absent. The resolver returns its verified snapshot
    // so skip/metadata planning does not read the entire file again.
    RecordingDestination resolved;
    bool existing_lossless=false,existing_vorbis=false;
    for(const auto codec:{Codec::Flac,Codec::Vorbis}) {
        if(c.mp3)break;
        if(c.lossless || (codec==Codec::Vorbis && !c.aac)) break;
        const auto extension=codec==Codec::Flac?L".flac":L".ogg";
        auto preferred=ResolveRecordingDestination(
            OutputPath(settings.root,catalog,extension,settings.music_folder,settings.track_number_in_filename),catalog,settings.track_number_in_filename,&directory);
        if(preferred.state==RecordingDestinationState::Blocked)
            return finish(Publication::Failed,"failed","preferred recording could not be inspected; publication deferred");
        if(preferred.state==RecordingDestinationState::Existing && preferred.recording.complete &&
           preferred.recording.quality.codec==codec) {
            existing_lossless=codec==Codec::Flac;
            existing_vorbis=codec==Codec::Vorbis;
            resolved=std::move(preferred);
            break;
        }
    }
    const bool preferred=existing_lossless||existing_vorbis;
    if(!preferred) resolved=ResolveRecordingDestination(
        capture_path,catalog,settings.track_number_in_filename,&directory);
    if(resolved.state==RecordingDestinationState::Blocked)
        return finish(Publication::Failed,"failed","no safe recording destination; existing files retained");
    if(settings.convert_numbered_filenames && !settings.track_number_in_filename) {
        const auto extension=existing_lossless?L".flac":existing_vorbis?L".ogg":c.Extension();
        if(ConvertResolvedFilename(resolved,catalog,settings,extension)==FilenameConversion::Failed)
            return finish(Publication::Failed,"failed","filename conversion failed; existing file retained");
    }
    const auto& destination=resolved.path;
    const auto& existing_path=resolved.path;
    const auto& inspected=resolved.recording;
    // These are planning decisions, not write authorization. PublishBytes
    // revalidates under its existing handle reservation before any replacement.
    auto decision=SaveDecision::NewFile;
    if(preferred || resolved.state==RecordingDestinationState::Existing) {
        decision=SaveDecision::Skip;
        if(!preferred && inspected.owned &&
           (!inspected.complete || HigherQuality(quality,inspected.quality)))
            decision=SaveDecision::Upgrade;
    }
    if(decision==SaveDecision::Skip && !format_upgrade) {
        // An audio duplicate may still have missing metadata. Update only a
        // verified matching recording; this never adds a Latest Downloads entry.
        const auto latest=GetSettings();
        const bool target_enabled=existing_lossless ? settings.flac : existing_vorbis?settings.ogg:FormatEnabled(c,settings);
        if(!c.mp3&&settings.metadata && settings.update_existing_metadata && target_enabled && latest.generation==settings.generation &&
           latest.downloads && latest.metadata && latest.update_existing_metadata) {
            const auto& path=existing_path;
            if(inspected.owned && inspected.complete && NeedsRecordingMetadata(inspected,tags)) {
                const auto result=UpdateRecordingMetadata(path,tags,catalog,settings.generation);
                if(result==FileMetadataUpdate::Updated) {
                    RefreshSongDestination();
                    return finish(Publication::Saved,"updated","metadata and artwork updated; existing audio retained");
                }
                if(result==FileMetadataUpdate::Failed)
                    return finish(Publication::Failed,"failed","metadata update failed; existing file retained");
            }
        }
        return finish(Publication::Skipped,"skipped",ExistingSkipReason(existing_path,quality,catalog,&inspected));
    }
    // Existing tags/art/lyrics are merged by PublishBytes while its exact old
    // object is locked. Do not take a separate stale preservation snapshot here.
    const bool had_artwork=inspected.has_artwork || superseded_vorbis.recording.has_artwork;
    resolved.recording={}; // No planning metadata retained across construction.
    std::vector<uint8_t> tagged; std::string error;
    if(c.aac) {
        const auto& info=c.spac.info;
        const auto count=info.samples_per_packet?
            (uint64_t(info.total_samples)+info.priming+info.samples_per_packet-1)/info.samples_per_packet:0;
        if(!AacExtractionFitsBudget(c.data.capacity(),c.data.size(),size_t(count)))
            return finish(Publication::Failed,"failed","AAC extraction memory budget exceeded; no file published");
    }
    // Every linked capture gets a final complete-buffer check immediately
    // before its block storage is cleared. Ogg/AAC are rebuilt from the cached
    // bytes; flattened FLAC/Ogg then receive the recording parser's whole-file
    // frame/page checksum and trailing-data validation as well.
    if(CachedFinalValidation(c)!=FinalCaptureValidation::Valid)
        return finish(Publication::Failed,"failed","final cached capture validation failed; no file published");
    auto source=c.data.Flatten();
    if(!ValidateFlattenedCapture(c,source))
        return finish(Publication::Failed,"failed","flattened capture integrity validation failed; no file published");
    bool tagged_ok=false;
    if(c.aac) {
        AacInfo info;std::vector<std::vector<uint8_t>> packets;
        tagged_ok=ReadSpac(source,info,packets,error);
        // Extraction has its own peak bound. Release flattened source bytes
        // before the muxer reserves output; compressed capture blocks were
        // already released by Flatten(). Only one publication runs at a time.
        std::vector<uint8_t>().swap(source);
        if(tagged_ok)tagged_ok=MakeAacM4a(info,packets,tags,tagged,error);
    } else if(c.mp3)tagged_ok=MakeOwnedMp3(source.data(),source.size(),tags,tagged,error);
    else tagged_ok=c.lossless ? TagFlac(source,tags,tagged,error) : TagOgg(source,tags,tagged,error);
    if(!tagged_ok)return finish(Publication::Failed,"failed","tagging failed: "+error);
    auto latest=GetSettings();
    if(latest.generation!=settings.generation || !latest.downloads || !FormatEnabled(c,latest)) return finish(Publication::Skipped,"skipped","capture settings changed or downloads/format disabled");
    if(!Directories(destination.substr(0,destination.find_last_of(L'\\')))) return finish(Publication::Failed,"failed","could not create save directory");
    // Only complete tagged audio reaches the bounded save worker's staged,
    // flushed publication protocol. Recording identity authorizes replacement.
    size_t published_bytes=0;
    auto publication=format_upgrade
        ?PublishFormatUpgrade(superseded_vorbis.path,destination,quality,tagged.data(),tagged.size(),catalog,&published_bytes)
        :PublishBytes(destination,quality,tagged.data(),tagged.size(),catalog,&published_bytes);
    if(publication==FilePublication::Skipped)return finish(Publication::Skipped,"skipped",ExistingSkipReason(destination,quality,catalog));
    if(publication==FilePublication::Failed)return finish(Publication::Failed,"failed","file publication failed; existing file retained");
    RefreshSongDestination();
    ImageInfo artwork;
    if(!ReadImageInfo(tags.cover,artwork) && !(publication==FilePublication::Upgraded && had_artwork))
        LogEvent(LogKind::Diagnostic,"metadata","artwork unavailable for completed publication");
    RecordRecentDownload(heard.media.artist+L" - "+heard.media.title,destination,published_bytes);
    return finish(Publication::Saved,publication==FilePublication::Upgraded ? "upgraded" : "finished",
        SavedAudioDetails(quality,published_bytes),publication==FilePublication::Saved);
}
struct SaveJob {std::unique_ptr<Capture> capture;Heard heard;unsigned epoch=0;size_t reserved=0;};
static BoundedQueue<SaveJob,2> saves;
static HANDLE save_event=nullptr;
static std::atomic<size_t> publishing_reserved{0};
static std::atomic<bool> workers_running{false};
static hooks::InitController audio_init;
// Existing-file checks share the publication worker, but never consume an audio
// queue slot. They perform optional filename conversion and metadata filling.
// Rapid track changes retain only one superseding request; serial cancellation
// rejects an older request after a new track/settings observation.
struct FilenameCheck {
    Catalog catalog;Tags tags;Settings settings;
    PlaybackQualitySnapshot quality;
    uint64_t media_identity=0;
    std::string playback_instance;
    uint64_t serial=0;
};
static SRWLOCK filename_check_lock=SRWLOCK_INIT;
static std::optional<FilenameCheck> pending_filename_check;
// Producer-only state: the capture worker owns these two fields.
static std::string last_filename_check_key;
static std::string last_auto_skip_playback;
static uint64_t last_filename_check_serial=0,queued_filename_check_serial=0;
static void CancelFilenameChecks() {
    filename_check_serial.fetch_add(1,std::memory_order_acq_rel);
    last_filename_check_key.clear();last_filename_check_serial=queued_filename_check_serial=0;
}
static void QueueFilenameCheck(const Media& media,const Settings& settings) {
    try {
        const bool convert=settings.convert_numbered_filenames&&!settings.track_number_in_filename;
        const bool update=settings.metadata&&settings.update_existing_metadata&&
            media.spotify_uri.rfind("spotify:episode:",0)!=0;
        const bool auto_skip=settings.auto_skip_downloaded&&media.source==MediaSource::Spotify&&
            media.spotify_uri.rfind("spotify:track:",0)==0&&!media.playback_instance.empty();
        if(!settings.downloads || (!convert&&!update&&!auto_skip) || media.title.empty() || !media.playing) {
            if(!last_filename_check_key.empty())CancelFilenameChecks();
            return;
        }
        const auto metadata_revision=update?MetadataRevision():0;
        const auto media_identity=PlaybackIdentity(media.Key());
        const auto quality=auto_skip?ReadPlaybackQuality():PlaybackQualitySnapshot{};
        auto catalog=CatalogFor(media);
        Tags tags;
        tags.fields={{"TITLE",Utf8(media.title)},{"ARTIST",Utf8(media.artist)},
            {"ALBUM",Utf8(media.album)},{"ALBUMARTIST",Utf8(media.album_artist)},
            {"TRACKNUMBER",std::to_string(media.track)}};
        if(!media.genre.empty())tags.fields.push_back({"GENRE",Utf8(media.genre)});
        EnrichTags(media,tags);ApplyCatalogIdentity(tags,catalog);
        std::string key;
        auto text=[&](const std::string& value){key+=std::to_string(value.size())+":"+value;};
        auto wide=[&](const std::wstring& value){text(Utf8(value));};
        // Use raw identity, not sanitized filenames: A/B and A:B may share a
        // basename but are still distinct recordings when Spotify URI is absent.
        const std::wstring* fields[]={&settings.root,&catalog.title,&catalog.artist,&catalog.folder_artist,&catalog.legacy_artist,&catalog.album,&catalog.author,&catalog.series,&catalog.book,&catalog.show};
        for(const auto* field:fields)wide(*field);
        text(catalog.spotify_uri);
        if(auto_skip) {
            text(media.playback_instance);
            key+=std::to_string(quality.identity)+":"+std::to_string(unsigned(quality.format))+":"+
                std::to_string(quality.rate)+":"+std::to_string(quality.channels)+":"+
                std::to_string(quality.bits)+":";
        }
        key+=std::to_string(unsigned(catalog.kind))+":"+std::to_string(catalog.track)+":"+
            std::to_string(catalog.disc)+":"+std::to_string(catalog.episode_year)+":"+
            std::to_string(catalog.duration)+":"+std::to_string(settings.generation)+":"+std::to_string(settings.music_folder)+":"+
            std::to_string(metadata_revision);
        // Invalidate the previous intent even if submission loses the try-lock.
        // Retrying this same intent preserves its serial instead of starving I/O.
        if(key!=last_filename_check_key || last_filename_check_serial!=filename_check_serial.load(std::memory_order_acquire)) {
            last_filename_check_serial=filename_check_serial.fetch_add(1,std::memory_order_acq_rel)+1;
            last_filename_check_key.swap(key);
        }
        if(queued_filename_check_serial==last_filename_check_serial)return;
        FilenameCheck check{std::move(catalog),std::move(tags),settings,quality,media_identity,
            media.playback_instance,last_filename_check_serial};
        if(!TryAcquireSRWLockExclusive(&filename_check_lock))return;
        struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&filename_check_lock);}} unlock;
        if(check.serial!=filename_check_serial.load(std::memory_order_acquire))return;
        pending_filename_check=std::move(check);
        queued_filename_check_serial=last_filename_check_serial;
        SetEvent(save_event);
    }catch(...) {CancelFilenameChecks(); /* Retry later; never leave an older request authorized. */}
}
static std::optional<FilenameCheck> TakeFilenameCheck() {
    ExclusiveSrw guard(filename_check_lock);
    auto check=std::move(pending_filename_check);pending_filename_check.reset();return check;
}
static void ProcessFilenameCheck(const FilenameCheck& check) {
    auto current=[&] {
        const auto settings=GetSettings();
        const bool convert=settings.convert_numbered_filenames&&!settings.track_number_in_filename;
        const bool update=settings.metadata&&settings.update_existing_metadata&&check.catalog.kind!=MediaKind::Podcast;
        const bool auto_skip=settings.auto_skip_downloaded&&check.catalog.kind!=MediaKind::Podcast&&
            !check.playback_instance.empty();
        return check.serial==filename_check_serial.load(std::memory_order_acquire) &&
            settings.generation==check.settings.generation && settings.downloads &&
            (convert||update||auto_skip);
    };
    if(!current())return;
    RecordingDirectory directory;
    if(!ListRecordingDirectory(OutputPath(check.settings.root,check.catalog,L".flac",check.settings.music_folder,false),directory,&check.catalog))return;
    for(const auto& format:kRecordingFormats) {
        if(!SupportsMedia(format,check.catalog.kind))continue;
        if(!current())return;
        auto resolved=ResolveRecordingDestination(OutputPath(check.settings.root,check.catalog,format.extension,check.settings.music_folder,false),check.catalog,false,&directory);
        if(!current())return;
        if(resolved.state==RecordingDestinationState::Blocked)return;
        if(resolved.state!=RecordingDestinationState::Existing || !resolved.recording.complete)continue;
        if(check.settings.convert_numbered_filenames&&!check.settings.track_number_in_filename&&
           ConvertResolvedFilename(resolved,check.catalog,check.settings,format.extension,check.serial)==FilenameConversion::Failed)
            LogEvent(LogKind::Failure,"failed","filename conversion deferred; existing recording retained | "+Utf8(check.catalog.title));
        if(!current())return;
        const bool format_enabled=format.codec==Codec::Flac?check.settings.flac:
            format.codec==Codec::Vorbis?check.settings.ogg:format.codec==Codec::Aac?check.settings.aac:false;
        bool metadata_failed=false;
        if(format_enabled&&check.settings.metadata&&check.settings.update_existing_metadata&&
           resolved.recording.owned&&NeedsRecordingMetadata(resolved.recording,check.tags)) {
            const auto result=UpdateRecordingMetadata(resolved.path,check.tags,check.catalog,check.settings.generation);
            const auto identity=Utf8(check.catalog.artist)+" - "+Utf8(check.catalog.title);
            if(result==FileMetadataUpdate::Updated) {
                RefreshSongDestination();
                LogEvent(LogKind::Activity,"updated","metadata and artwork updated; existing audio retained | "+identity);
            } else if(result==FileMetadataUpdate::Failed) {
                metadata_failed=true;
                LogEvent(LogKind::Failure,"failed","metadata update failed; existing file retained | "+identity);
            }
        }
        if(!current())return;
        const auto playback_key=check.catalog.spotify_uri+"\x1f"+check.playback_instance;
        if(!metadata_failed&&resolved.recording.owned&&check.settings.auto_skip_downloaded&&
           check.catalog.kind!=MediaKind::Podcast&&playback_key!=last_auto_skip_playback&&
           ShouldAutoSkipDownloaded(check.quality,check.media_identity,resolved.recording.quality)) {
            const auto token=QueueAutoSkipRequest(check.catalog.spotify_uri,check.playback_instance,
                check.settings.generation);
            if(token) {
                last_auto_skip_playback=playback_key;
                LogEvent(LogKind::Activity,"auto-skip","requested after existing-file metadata check | "+
                    Utf8(check.catalog.artist)+" - "+Utf8(check.catalog.title));
            }
        }
        return; // Best verified format/recording found; no new audio is created.
    }
}
static DWORD WINAPI SaveWorker(LPVOID) {
    while(workers_running.load(std::memory_order_acquire)) {
        SaveJob job;
        if(!saves.Pop(job)) {
            auto check=TakeFilenameCheck();
            if(check) {
                try {ProcessFilenameCheck(*check);}catch(...) {Log(LogKind::Failure,"filename check failed; existing recordings retained");}
                continue;
            }
            WaitForSingleObject(save_event,200);continue;
        }
        std::string identity=Utf8(job.heard.media.artist)+" - "+Utf8(job.heard.media.title);
        try {
            Publish(*job.capture,job.heard,job.epoch);
        } catch(...) {LogEvent(LogKind::Failure,"failed",identity+" (publication exception)");}
        job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
    }
    return 0;
}
struct CaptureBuffers {
    ownership::DecoderOwnershipTracker* ownership_tracker=nullptr;
    uint64_t ownership_hold_generation=0;
    uint64_t pending_natural_generation=0;
    uint64_t confirmed_transition_playback=0;
    bool unidentified_interstitial_active=false;
    bool unidentified_advertisement=false;
    std::string unidentified_successor_uri;
    std::string unidentified_successor_instance;
    double pending_natural_time=0;
    std::string pending_natural_playback_instance;
    Listen pending_natural_listen{};
    std::map<uintptr_t,std::unique_ptr<Capture>> active;
    struct OggKey {
        uintptr_t context=0;uint32_t serial=0;
        bool operator<(const OggKey& other) const noexcept {
            return context<other.context || (context==other.context&&serial<other.serial);
        }
    };
    std::map<OggKey,std::unique_ptr<Capture>> ogg_active;
    std::vector<std::unique_ptr<Capture>> ready;
    // Lightweight evidence boundaries survive removal of unmatched bytes/records.
    double lost_capture_start_through=-std::numeric_limits<double>::infinity();
    double lost_listen_born_through=-std::numeric_limits<double>::infinity();
    struct LostActiveEvidence {double born=0,duration=0;};
    std::array<LostActiveEvidence,32> lost_active{};
    unsigned lost_active_count=0,next_lost_active=0;
    void RecordLostActive(Capture* capture,
                          ownership::CaptureAbortReason reason=ownership::CaptureAbortReason::InvalidData) {
        if(!capture)return;
        if(capture->mp3)ReleaseMp3Probe(capture->capture_context,capture->capture_epoch);
        if(ownership_tracker&&capture->capture_token) {
            ownership_tracker->AbortCapture(capture->capture_token,reason);
            if(DebugLoggingEnabled()) {
                const auto binding=ownership_tracker->Binding(capture->capture_token);
                char line[260];snprintf(line,sizeof(line),
                    "ownership event=capture-abort capture=%llu owner=%llu abort_reason=%u state=%u state_reason=%s",
                    static_cast<unsigned long long>(binding.capture_token),
                    static_cast<unsigned long long>(binding.owner_token),unsigned(reason),unsigned(binding.state),
                    ownership::OwnershipReasonName(binding.reason));
                Log(LogKind::Diagnostic,line);
            }
        }
        CheckBeforeRelease(*capture,"active capture discarded",ownership_tracker);
        if(lost_active_count==lost_active.size())
            lost_capture_start_through=std::max(lost_capture_start_through,lost_active[next_lost_active].born+20);
        else ++lost_active_count;
        lost_active[next_lost_active]={capture->born,DeclaredCaptureDuration(*capture)};
        next_lost_active=(next_lost_active+1)%lost_active.size();
    }
    bool LostActiveMatches(const Heard& heard) const {
        for(unsigned i=0;i<lost_active_count;++i) {
            const auto& evidence=lost_active[i];
            if(evidence.born>=heard.start-20&&evidence.born<=heard.start+3&&evidence.born<heard.finish&&
               (!evidence.duration||std::fabs(evidence.duration-heard.media.duration)<=1.0))return true;
        }
        return false;
    }
};
static void ClearPendingNaturalTransition(CaptureBuffers& buffers) noexcept {
    buffers.pending_natural_generation=0;buffers.pending_natural_time=0;
    buffers.pending_natural_playback_instance.clear();buffers.pending_natural_listen={};
}
static bool ExcludeUnboundMp3Interludes(CaptureBuffers& buffers,const Media& media) {
    if(media.spotify_uri.rfind("spotify:track:",0)!=0||!buffers.ownership_tracker)return false;
    bool excluded=false;
    for(auto it=buffers.active.begin();it!=buffers.active.end();) {
        auto& capture=*it->second;
        if(!capture.mp3||!capture.capture_token||
           buffers.ownership_tracker->Binding(capture.capture_token).state==ownership::OwnershipState::Bound) {
            ++it;continue;
        }
        ReleaseMp3Probe(capture.capture_context,capture.capture_epoch);
        buffers.ownership_tracker->AbortCapture(
            capture.capture_token,ownership::CaptureAbortReason::InvalidData);
        if(DebugLoggingEnabled())Log(LogKind::Diagnostic,
            "excluded unbound MP3 interlude before music playback");
        it=buffers.active.erase(it);excluded=true;
    }
    return excluded;
}
static bool SpotifyCurrentItemChanged(const Media& previous,const Media& next) {
    if(previous.source!=MediaSource::Spotify||next.source!=MediaSource::Spotify||
       previous.spotify_uri.empty()||next.spotify_uri.rfind("spotify:track:",0)!=0||
       next.playback_instance.empty()||!next.playing)return false;
    return previous.spotify_uri!=next.spotify_uri||
        (!previous.playback_instance.empty()&&
         previous.playback_instance!=next.playback_instance);
}
static void ClearUnidentifiedSuccessor(CaptureBuffers& buffers) {
    buffers.unidentified_interstitial_active=false;
    buffers.unidentified_advertisement=false;
    buffers.unidentified_successor_uri.clear();
    buffers.unidentified_successor_instance.clear();
}
static bool ActiveInterstitialReason(const char* reason) noexcept {
    return reason&&(std::strcmp(reason,"client-active-advertisement")==0||
                   std::strcmp(reason,"client-active-interstitial")==0||
                   std::strcmp(reason,"client-active-without-uri")==0);
}
static void MarkUnidentifiedInterstitial(CaptureBuffers& buffers,const char* reason) {
    const bool advertisement=reason&&std::strcmp(reason,"client-active-advertisement")==0;
    const bool changed=!buffers.unidentified_interstitial_active||
        buffers.unidentified_advertisement!=advertisement;
    buffers.unidentified_interstitial_active=true;
    buffers.unidentified_advertisement=advertisement;
    buffers.unidentified_successor_uri.clear();
    buffers.unidentified_successor_instance.clear();
    if(changed)LogEvent(LogKind::Activity,"ignored",advertisement?
        "Spotify advertisement (audio excluded from downloads)":
        "Spotify interstitial without a supported URI (audio excluded from downloads)");
}
static bool PrepareUnidentifiedSuccessor(CaptureBuffers& buffers,const Media& previous,
                                         const Media& next) {
    if(!buffers.unidentified_interstitial_active)return false;
    if(next.explicit_skip||next.explicit_control||next.prior_control) {
        ClearUnidentifiedSuccessor(buffers);return false;
    }
    if(buffers.unidentified_successor_uri.empty()) {
        if(!SpotifyCurrentItemChanged(previous,next))return false;
        buffers.unidentified_successor_uri=next.spotify_uri;
        buffers.unidentified_successor_instance=next.playback_instance;
    }
    if(next.playing&&next.spotify_uri==buffers.unidentified_successor_uri&&
       next.playback_instance==buffers.unidentified_successor_instance)return true;
    if(next.source==MediaSource::Spotify&&next.playing&&
       next.spotify_uri.rfind("spotify:track:",0)==0)
        ClearUnidentifiedSuccessor(buffers);
    return false;
}
static bool BoundCaptureProvesInterludeSuccessor(CaptureBuffers& buffers,const Media& media,
                                                 bool interlude_excluded,
                                                 bool reconcile_interstitial) {
    if(!interlude_excluded||!buffers.ownership_tracker||media.playback_instance.empty()||!media.playing||
       media.position<=1.5||media.spotify_uri.rfind("spotify:track:",0)!=0)return false;
    const auto playback=buffers.ownership_tracker->PlaybackToken(media.playback_instance);
    const bool confirmed_transition=playback&&buffers.confirmed_transition_playback==playback;
    if(!confirmed_transition&&!reconcile_interstitial)return false;
    size_t candidates=0;Capture* candidate=nullptr;
    const auto inspect=[&](Capture& capture) {
        const auto declared=DeclaredCaptureDuration(capture);
        const bool began_at_source_start=capture.lossless?(capture.metadata&&capture.coverage.frames):
            capture.aac?(capture.spac.info.rate&&capture.SourceUnits()):
            (capture.stream.pages&&(capture.stream.active||capture.stream.complete));
        if(capture.mp3||capture.ambiguous||!capture.capture_token||!began_at_source_start||
           (declared&&std::fabs(declared-media.duration)>1.0))return;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(binding.state==ownership::OwnershipState::Bound&&binding.playback_token!=playback)return;
        if(!declared&&(capture.lossless||capture.aac||!capture.stream.pages))return;
        const bool recoverable=reconcile_interstitial&&
            binding.state==ownership::OwnershipState::Conflict&&
            binding.reason==ownership::OwnershipReason::AmbiguousSuccessor;
        if(binding.state!=ownership::OwnershipState::Bound&&
           binding.state!=ownership::OwnershipState::AwaitingPlaybackInstance&&!recoverable)return;
        candidate=&capture;++candidates;
    };
    for(auto& capture:buffers.active)inspect(*capture.second);
    for(auto& capture:buffers.ogg_active)inspect(*capture.second);
    for(auto& capture:buffers.ready)inspect(*capture);
    if(candidates!=1||!candidate)return false;
    const auto binding=buffers.ownership_tracker->Binding(candidate->capture_token);
    return binding.state!=ownership::OwnershipState::Conflict||
        buffers.ownership_tracker->ReconsiderAmbiguousSuccessor(candidate->capture_token);
}
static uint64_t BindReadyCapture(CaptureBuffers& buffers,const Media& media,
                                 uint64_t generation,double observed,
                                 uint64_t confirmed_transition_playback=0,
                                 const std::string& transition_uri={},
                                 bool reconcile_interstitial=false) {
    if(!buffers.ownership_tracker||!generation||!media.playing||media.duration<=0)return 0;
    const auto& resolved_uri=media.spotify_uri.empty()?transition_uri:media.spotify_uri;
    const bool episode=resolved_uri.rfind("spotify:episode:",0)==0;
    const bool track=resolved_uri.rfind("spotify:track:",0)==0;
    const bool kind_unknown=resolved_uri.empty();
    if(!episode&&!track&&!kind_unknown)return 0;
    const auto ready_playback=media.playback_instance.empty()?0:
        buffers.ownership_tracker->PlaybackToken(media.playback_instance);
    const bool confirmed_transition=ready_playback&&
        ready_playback==confirmed_transition_playback;
    Capture* candidate=nullptr;size_t candidates=0;
    uint64_t already_bound=0;
    const auto inspect=[&](Capture& capture) {
        if(capture.ambiguous||!capture.decoder_owner||!capture.capture_token||
           (kind_unknown?capture.mp3:capture.mp3!=episode))return;
        const auto declared=DeclaredCaptureDuration(capture);
        const bool active_ogg_without_duration=!declared&&!capture.lossless&&!capture.aac&&
            !capture.mp3&&capture.stream.pages&&confirmed_transition;
        const bool active_mp3_without_duration=!declared&&episode&&capture.mp3&&
            !capture.finished&&capture.bytes;
        if((!declared&&!active_ogg_without_duration&&!active_mp3_without_duration)||
           (declared&&std::fabs(declared-media.duration)>1.0))return;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(binding.state==ownership::OwnershipState::Bound) {
            if(binding.listen_generation==generation) {
                capture.listen_generation=generation;
                if(!already_bound)already_bound=binding.playback_token;
                else if(already_bound!=binding.playback_token)already_bound=~uint64_t(0);
            } else if(!binding.listen_generation&&ready_playback&&
                      binding.playback_token==ready_playback) {
                candidate=&capture;++candidates;
            }
            return;
        }
        const bool recoverable_successor=reconcile_interstitial&&track&&
            !media.playback_instance.empty()&&
            binding.state==ownership::OwnershipState::Conflict&&
            binding.reason==ownership::OwnershipReason::AmbiguousSuccessor;
        if(binding.state==ownership::OwnershipState::AwaitingPlaybackInstance||recoverable_successor) {
            candidate=&capture;++candidates;
        }
    };
    for(auto& capture:buffers.active)inspect(*capture.second);
    for(auto& capture:buffers.ogg_active)inspect(*capture.second);
    for(auto& capture:buffers.ready)inspect(*capture);
    if(already_bound&&already_bound!=~uint64_t(0))return already_bound;
    if(candidates!=1||!candidate)return 0;
    const auto selected=buffers.ownership_tracker->Binding(candidate->capture_token);
    if(selected.state==ownership::OwnershipState::Conflict&&
       !buffers.ownership_tracker->ReconsiderAmbiguousSuccessor(candidate->capture_token))return 0;
    if(!buffers.ownership_tracker->BindCapture(candidate->capture_token,
        media.playback_instance,resolved_uri,generation,observed))return 0;
    candidate->listen_generation=generation;
    return buffers.ownership_tracker->Binding(candidate->capture_token).playback_token;
}
static uint64_t BindDirectReadyCapture(
    CaptureBuffers& buffers,const Media& media,uint64_t generation,double observed,
    const std::array<playback_identity::Match,playback_identity::kObservationCapacity>& matches,
    size_t match_count) {
    if(!buffers.ownership_tracker||!generation||!media.playing||media.playback_instance.empty()||
       !match_count)return 0;
    const bool episode=media.spotify_uri.rfind("spotify:episode:",0)==0;
    const bool track=media.spotify_uri.rfind("spotify:track:",0)==0;
    if(!episode&&!track)return 0;
    const auto expected_playback=buffers.ownership_tracker->PlaybackToken(media.playback_instance);
    Capture* candidate=nullptr;bool multiple=false;
    const auto inspect=[&](Capture& capture) {
        if(capture.ambiguous||!capture.capture_token||!capture.decoder_owner||
           capture.mp3!=episode)return;
        bool exact=false;
        for(size_t index=0;index<match_count;++index)
            if(matches[index].metadata_generation==generation&&
               matches[index].role==playback_identity::SnapshotRole::PlayerPlaybackId&&
               matches[index].decoder==capture.decoder_owner) {
                exact=true;break;
            }
        if(!exact)return;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(binding.state==ownership::OwnershipState::Bound) {
            if((!expected_playback||binding.playback_token==expected_playback)&&
               (!binding.listen_generation||binding.listen_generation==generation))candidate=&capture;
            return;
        }
        if(binding.state!=ownership::OwnershipState::AwaitingPlaybackInstance&&
           !(binding.state==ownership::OwnershipState::Conflict&&
             binding.reason==ownership::OwnershipReason::AmbiguousSuccessor))return;
        if(candidate&&candidate!=&capture)multiple=true;
        else candidate=&capture;
    };
    for(auto& capture:buffers.active)inspect(*capture.second);
    for(auto& capture:buffers.ogg_active)inspect(*capture.second);
    for(auto& capture:buffers.ready)inspect(*capture);
    if(!candidate||multiple)return 0;
    if(!buffers.ownership_tracker->BindCaptureExact(candidate->capture_token,
        media.playback_instance,media.spotify_uri,generation,observed))return 0;
    candidate->listen_generation=generation;
    const auto binding=buffers.ownership_tracker->Binding(candidate->capture_token);
    if(DebugLoggingEnabled()) {
        char line[220];snprintf(line,sizeof(line),
            "identity-bridge event=bound capture=%llu playback=%llu listen_generation=%llu",
            static_cast<unsigned long long>(candidate->capture_token),
            static_cast<unsigned long long>(binding.playback_token),
            static_cast<unsigned long long>(generation));
        Log(LogKind::Diagnostic,line);
    }
    return binding.playback_token;
}
static void CanonicalizeBoundReadyIdentity(Media& media,Listen& listen,
                                           const std::string& uri,uint64_t playback_token) {
    if(!playback_token||uri.empty())return;
    if(media.spotify_uri.empty())media.spotify_uri=uri;
    if(media.source==MediaSource::Windows&&listen.generation)
        listen.identity=media.ListenKey();
}
static void CarryBoundReadyIdentity(Media& media,const Media& current,
                                    bool successor_transition_pending) {
    if(successor_transition_pending||media.source!=MediaSource::Windows||
       current.source!=MediaSource::Windows||!media.spotify_uri.empty()||
       current.spotify_uri.empty()||media.duration<=0||current.duration<=0||
       std::fabs(media.duration-current.duration)>1.0||
       media.position<current.position-1.5)return;
    media.spotify_uri=current.spotify_uri;
}
static void RememberBoundCaptureIdentity(CaptureBuffers& buffers,const Media& media,
                                         uint64_t generation,uint64_t playback_token) {
    if(!buffers.ownership_tracker||!generation||!playback_token)return;
    const auto identity=Utf8(media.artist)+" - "+Utf8(media.title);
    const auto remember=[&](Capture& capture) {
        if(!capture.capture_token)return;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(binding.state==ownership::OwnershipState::Bound&&
           binding.listen_generation==generation&&binding.playback_token==playback_token)
            capture.bound_identity=identity;
    };
    for(auto& capture:buffers.active)remember(*capture.second);
    for(auto& capture:buffers.ogg_active)remember(*capture.second);
    for(auto& capture:buffers.ready)remember(*capture);
}
static void BeginCaptureOwnership(Capture& capture,CaptureBuffers& buffers,
                                  ownership::DecoderCodec codec,uintptr_t context,
                                  uint32_t stream,double time,unsigned epoch,uint64_t sequence=0) {
    // A discovered layout describes addresses; a retained decoded target proves
    // the observation hook was prepared and the successful transaction made it
    // usable before any capture callback can run.
    const bool observing=decoded_sink_target!=nullptr;
    int32_t delta=0;bool verified=decoder_ownership_layout_available&&observing;
    if(codec==ownership::DecoderCodec::Ogg)delta=decoder_ownership_layout.ogg_capture_to_decoder;
    else if(codec==ownership::DecoderCodec::Flac)delta=decoder_ownership_layout.flac_capture_to_decoder;
    else if(codec==ownership::DecoderCodec::Aac)verified=verified&&decoder_ownership_layout.aac_verified;
    else verified=observing; // The decoded dispatcher context is the MP3 decoder owner.
    uintptr_t owner=0;
    if(verified)ownership::NormalizeDecoderOwner(context,delta,owner);
    capture.decoder_owner=owner;
    if(buffers.ownership_tracker) {
        capture.capture_token=buffers.ownership_tracker->BeginCapture(codec,owner,stream,time,epoch,sequence);
        if(DebugLoggingEnabled()) {
            const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
            const char* name=codec==ownership::DecoderCodec::Flac?"flac":
                codec==ownership::DecoderCodec::Ogg?"ogg":codec==ownership::DecoderCodec::Aac?"aac":"mp3";
            char line[280];snprintf(line,sizeof(line),
                "ownership event=capture-begin capture=%llu owner=%llu codec=%s stream=%u epoch=%u state=%u reason=%s",
                static_cast<unsigned long long>(binding.capture_token),
                static_cast<unsigned long long>(binding.owner_token),name,stream,epoch,unsigned(binding.state),
                ownership::OwnershipReasonName(binding.reason));
            Log(LogKind::Diagnostic,line);
        }
    }
}
static void CompleteCaptureOwnership(Capture& capture,CaptureBuffers& buffers,double time,
                                     uint64_t sequence=0) {
    if(buffers.ownership_tracker&&capture.capture_token) {
        buffers.ownership_tracker->CompleteCapture(capture.capture_token,time,sequence);
        if(DebugLoggingEnabled()) {
            const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
            char line[280];snprintf(line,sizeof(line),
                "ownership event=capture-complete capture=%llu owner=%llu state=%u reason=%s bytes=%llu mono=%.3f",
                static_cast<unsigned long long>(binding.capture_token),
                static_cast<unsigned long long>(binding.owner_token),unsigned(binding.state),
                ownership::OwnershipReasonName(binding.reason),
                static_cast<unsigned long long>(capture.bytes),time);
            Log(LogKind::Diagnostic,line);
        }
    }
}
static void LogOwnershipBindings(CaptureBuffers& buffers) {
    if(!DebugLoggingEnabled()||!buffers.ownership_tracker)return;
    const auto observe=[&](Capture& capture) {
        if(!capture.capture_token)return;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(capture.reported_ownership_state==unsigned(binding.state)&&
           capture.reported_ownership_reason==unsigned(binding.reason)&&
           capture.reported_playback_token==binding.playback_token&&
           capture.reported_generation==binding.listen_generation)return;
        capture.reported_ownership_state=unsigned(binding.state);
        capture.reported_ownership_reason=unsigned(binding.reason);
        capture.reported_playback_token=binding.playback_token;
        capture.reported_generation=binding.listen_generation;
        char line[300];snprintf(line,sizeof(line),
            "ownership event=binding capture=%llu owner=%llu instance=%llu listen_generation=%llu state=%u reason=%s",
            static_cast<unsigned long long>(binding.capture_token),
            static_cast<unsigned long long>(binding.owner_token),
            static_cast<unsigned long long>(binding.playback_token),
            static_cast<unsigned long long>(binding.listen_generation),unsigned(binding.state),
            ownership::OwnershipReasonName(binding.reason));
        Log(LogKind::Diagnostic,line);
    };
    for(auto& capture:buffers.active)observe(*capture.second);
    for(auto& capture:buffers.ogg_active)observe(*capture.second);
    for(auto& capture:buffers.ready)observe(*capture);
}
static void ObserveCodecQuality(const Slot& s,PlaybackQualityTracker& quality) {
    switch(s.kind) {
        case AudioEventKind::OggPage: quality.Ogg(s.context,s.bytes,s.length,s.time); break;
        case AudioEventKind::FlacBegin: quality.FlacBegin(s.context,s.time); break;
        case AudioEventKind::FlacBytes: quality.FlacBytes(s.context,s.bytes,s.length,size_t(s.input_length)); break;
        case AudioEventKind::FlacFrame: quality.FlacFrame(s.context,s.bytes,s.length); break;
        case AudioEventKind::FlacError: quality.FlacEnd(s.context,true); break;
        case AudioEventKind::FlacEnd: quality.FlacEnd(s.context,false); break;
        default:break;
    }
}
static const char* ErrorOrigin(FlacErrorSource source) {
    switch(source) {
        case FlacErrorSource::ReadCallback:return "native-read-return";
        case FlacErrorSource::FrameCallback:return "native-frame-return";
        case FlacErrorSource::DecoderCallback:return "native-error-callback";
        default:return "unknown";
    }
}
static void LogCaptureContext(const Slot& s,const Capture* capture,const char* origin,const char* extra="") {
    if(!DebugLoggingEnabled())return;
    char line[760];snprintf(line,sizeof(line),
        "FLAC observation origin=%s ctx=%llx mono=%.3f epoch=%u code=%u requested=%llu returned=%llu age=%.3f queue_lag_ms=%.1f metadata=%d frames=%u covered=%llu total=%llu bytes=%llu dropped=%ld%s",
        origin,static_cast<unsigned long long>(s.context),s.time,s.epoch,s.format,
        static_cast<unsigned long long>(s.requested),static_cast<unsigned long long>(s.returned),
        capture?s.time-capture->born:0,std::max(0.0,Now()-s.time)*1000,capture&&capture->metadata,
        capture?capture->coverage.frames:0,static_cast<unsigned long long>(capture?capture->coverage.samples:0),
        static_cast<unsigned long long>(capture?capture->flac.total_samples:0),static_cast<unsigned long long>(capture?capture->bytes:0),dropped,extra);
    Log(LogKind::Diagnostic,line);
}
static bool RestartFlacCaptureAtSampleZero(Capture& capture,const Slot& slot,
                                           CaptureBuffers& buffers) {
    if(!capture.metadata||!capture.flac_audio_offset||
       capture.flac_audio_offset>=capture.data.size())return false;
    size_t offset=0;
    const Quality quality{Codec::Flac,capture.flac.rate,capture.flac.channels,
                          capture.flac.bits,0};
    if(!FindValidatedFlacFrame(capture.data,quality,capture.flac.max_block,
                               0,0,capture.flac_audio_offset+1,offset)||
       !capture.data.RetainPrefixAndSuffix(capture.flac_audio_offset,offset))return false;
    if(buffers.ownership_tracker&&capture.capture_token)
        buffers.ownership_tracker->AbortCapture(
            capture.capture_token,ownership::CaptureAbortReason::Reinitialized);
    capture.bytes=capture.data.size();
    capture.coverage={};capture.born=slot.time;capture.finished=0;
    capture.ambiguous=false;capture.final_validation=-1;capture.validation_runs=0;
    capture.publication_deferred=false;capture.bound_identity.clear();
    capture.capture_token=0;capture.decoder_owner=0;
    BeginCaptureOwnership(capture,buffers,ownership::DecoderCodec::Flac,
                          slot.context,0,slot.time,slot.epoch,slot.sequence);
    if(DebugLoggingEnabled())Log(LogKind::Diagnostic,
        "FLAC sample-zero restart began a fresh compressed capture");
    return true;
}
static void ProcessFlacCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    auto found=active.find(s.context);
    if(s.kind==AudioEventKind::FlacError && (found==active.end()||!found->second->lossless))
        LogCaptureContext(s,nullptr,ErrorOrigin(s.error_source));
    if(s.kind==AudioEventKind::FlacBegin) {
        if(found!=active.end())buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::Reinitialized);
        if(found!=active.end())LogCaptureContext(s,found->second.get(),"context-reinitialized");
        auto c=std::make_unique<Capture>(); c->lossless=true; c->born=s.time;
        BeginCaptureOwnership(*c,buffers,ownership::DecoderCodec::Flac,s.context,0,s.time,s.epoch,s.sequence);
        active[s.context]=std::move(c);return;
    }
    if(found==active.end() || !found->second->lossless) return;
    Capture& c=*found->second;
    if(s.kind==AudioEventKind::FlacError) {
        LogCaptureContext(s,&c,ErrorOrigin(s.error_source));
        buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::DecoderError);active.erase(found); Log(LogKind::Failure,"FLAC decoder error; stream discarded"); return;
    }
    if(s.kind==AudioEventKind::FlacBytes) {
        size_t reserved=publishing_reserved.load(); for(const auto& item:active) reserved+=item.second->Capacity();
        for(const auto& item:buffers.ogg_active) reserved+=item.second->Capacity();
        for(const auto& item:ready) reserved+=item->Capacity();
        size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
        if(!c.data.Append(s.bytes,s.length,available)) { buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::MemoryLimit);active.erase(found); Log(LogKind::Failure,"FLAC capture memory limit; stream discarded"); return; }
        c.bytes+=s.length;
        if(!c.metadata) {
            size_t audio=0; auto parsed=ParseFlacMetadata(c.data,c.flac,audio);
            if(parsed==FlacParse::Invalid) { buffers.RecordLostActive(found->second.get());active.erase(found); Log(LogKind::Failure,"FLAC metadata invalid; stream discarded"); return; }
            if(parsed==FlacParse::Valid) {
                c.metadata=true;c.flac_audio_offset=audio; char line[180]; snprintf(line,sizeof(line),"FLAC STREAMINFO rate=%u channels=%u bits=%u total=%llu metadata_bytes=%zu",c.flac.rate,c.flac.channels,c.flac.bits,static_cast<unsigned long long>(c.flac.total_samples),audio); Log(LogKind::Diagnostic,line);
            }
        }
        return;
    }
    if(s.kind==AudioEventKind::FlacFrame) {
        uint32_t frame[6]; uint64_t number; memcpy(frame,s.bytes,24); memcpy(&number,s.bytes+24,8);
        if(frame[5]==0) number=uint32_t(number);
        const char* reason="metadata-not-ready";
        bool accepted=c.metadata&&c.coverage.Frame(
            c.flac,frame[0],frame[1],frame[2],frame[4],frame[5],number,&reason);
        if(!accepted&&c.metadata&&reason&&!std::strcmp(reason,"sample-position")&&
           number==0&&c.coverage.samples&&RestartFlacCaptureAtSampleZero(c,s,buffers))
            accepted=c.coverage.Frame(
                c.flac,frame[0],frame[1],frame[2],frame[4],frame[5],number,&reason);
        if(!accepted) {
            if(DebugLoggingEnabled()) {
                char line[400];snprintf(line,sizeof(line),
                    " reason=%s native_frame_result=0 expected_rate=%u got_rate=%u expected_channels=%u got_channels=%u expected_bits=%u got_bits=%u block=%u min_block=%u max_block=%u number_type=%u number=%llu expected_sample=%llu",
                    reason,c.flac.rate,frame[1],c.flac.channels,frame[2],c.flac.bits,frame[4],
                    frame[0],c.flac.min_block,c.flac.max_block,frame[5],static_cast<unsigned long long>(number),
                    static_cast<unsigned long long>(c.coverage.samples));
                LogCaptureContext(s,&c,"capture-coverage-guard",line);
            }
            buffers.RecordLostActive(found->second.get());active.erase(found); Log(LogKind::Failure,"FLAC decoded-frame gap or format mismatch; stream discarded"); return;
        }
        if(!c.coverage.Complete(c.flac)) return;
    } else if(s.kind!=AudioEventKind::FlacEnd || !c.coverage.Complete(c.flac)) {
        LogCaptureContext(s,&c,"end-before-coverage");
        buffers.RecordLostActive(found->second.get());active.erase(found); Log(LogKind::Failure,"FLAC truncated before total samples; discarded"); return;
    }
    c.finished=s.time;
    CompleteCaptureOwnership(c,buffers,s.time,s.sequence);
    char line[180]; snprintf(line,sizeof(line),"FLAC COMPLETE frames=%u samples=%llu bytes=%llu duration=%.6f",c.coverage.frames,static_cast<unsigned long long>(c.coverage.samples),static_cast<unsigned long long>(c.bytes),c.Duration()); Log(LogKind::Diagnostic,line);
    ready.push_back(std::move(found->second)); active.erase(found); return;
}
static void LogOggRejection(const Slot& slot,const Capture* capture,const Stream* previous,
                            const Page* page,OggRejection reason) {
    if(!DebugLoggingEnabled())return;
    const Stream empty{};const auto& stream=previous?*previous:empty;
    char line[640];snprintf(line,sizeof(line),
        "Ogg observation reason=%s ctx=%llx epoch=%u capture_known=%d page_known=%d expected_serial=%u actual_serial=%u expected_seq=%u actual_seq=%u previous_granule=%lld incoming_granule=%lld flags=%u rate=%u channels=%u pages=%u bytes=%llu capture_age_ms=%.1f dropped_total=%ld",
        OggRejectionName(reason),static_cast<unsigned long long>(slot.context),slot.epoch,capture!=nullptr,page!=nullptr,
        stream.serial,page?page->serial:0,stream.next,page?page->sequence:0,
        static_cast<long long>(stream.samples),static_cast<long long>(page?page->granule:-1),
        page?unsigned(page->flags):0,stream.rate,unsigned(stream.channels),stream.pages,
        static_cast<unsigned long long>(capture?capture->bytes:0),capture?(slot.time-capture->born)*1000:0,dropped);
    Log(LogKind::Diagnostic,line);
}
static void ProcessOggCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.ogg_active;auto& ready=buffers.ready;
    Page page; if(!ParsePage(s.bytes,s.length,page)) {
        uint32_t serial=0,sequence=0;bool isolated=false;
        if(ParsePageIdentity(s.bytes,s.length,serial,sequence)) {
            const CaptureBuffers::OggKey key{s.context,serial};auto damaged=active.find(key);
            if(damaged==active.end())isolated=true; // Invalid page for an inactive serial cannot poison peers.
            else {
                buffers.RecordLostActive(damaged->second.get(),ownership::CaptureAbortReason::InvalidData);active.erase(damaged);isolated=true;
            }
        }
        if(!isolated)for(auto it=active.begin();it!=active.end();) {
            if(it->first.context==s.context){buffers.RecordLostActive(it->second.get());it=active.erase(it);}else ++it;
        }
        LogOggRejection(s,nullptr,nullptr,nullptr,OggRejection::InvalidPage);
        Log(LogKind::Failure,"invalid Ogg page rejected"); return;
    }
    const CaptureBuffers::OggKey key{s.context,page.serial};
    if(page.vorbis_start) {
        size_t streams=0;for(const auto& item:active)streams+=item.first.context==s.context;
        if(active.find(key)==active.end()&&streams>=16) {
            LogOggRejection(s,nullptr,nullptr,&page,OggRejection::Inactive);
            Log(LogKind::Failure,"Ogg logical stream limit reached; new stream ignored");return;
        }
        const auto previous=active.find(key);
        if(previous!=active.end())buffers.RecordLostActive(previous->second.get(),ownership::CaptureAbortReason::Reinitialized);
        auto c=std::make_unique<Capture>(); c->born=s.time;
        BeginCaptureOwnership(*c,buffers,ownership::DecoderCodec::Ogg,s.context,page.serial,s.time,s.epoch,s.sequence);
        active[key]=std::move(c);
        char line[160]; snprintf(line,sizeof(line),"BOS ctx=%llx seq=%u serial=%u rate=%u channels=%u",
            static_cast<unsigned long long>(s.context),page.sequence,page.serial,page.rate,page.channels); Log(LogKind::Diagnostic,line);
    }
    auto found=active.find(key);
    if(found==active.end()) {
        LogOggRejection(s,nullptr,nullptr,&page,OggRejection::Inactive);return;
    }
    Capture& c=*found->second;Stream previous;
    const bool diagnostic=DebugLoggingEnabled();if(diagnostic)previous=c.stream;
    Result result=c.stream.Push(s.bytes,s.length);
    if(result==Result::Invalid || result==Result::Ignore) {
        LogOggRejection(s,&c,diagnostic?&previous:nullptr,&page,c.stream.rejection);
        char message[160];snprintf(message,sizeof(message),"Ogg stream discarded: %s; replay from the beginning to save",OggRejectionName(c.stream.rejection));
        buffers.RecordLostActive(found->second.get());active.erase(found);Log(LogKind::Failure,message);return;
    }
    size_t reserved=publishing_reserved.load();
    for(const auto& item:buffers.active) reserved+=item.second->Capacity();
    for(const auto& item:active)reserved+=item.second->Capacity();
    for(const auto& item:ready) reserved+=item->Capacity();
    size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
    if(!c.data.Append(s.bytes,s.length,available)) {
        buffers.RecordLostActive(found->second.get());active.erase(found);
        Log(LogKind::Failure,"capture memory limit reached; track discarded without scratch files"); return;
    }
    c.bytes+=s.length;
    if(result==Result::Complete) {
        c.finished=s.time;
        CompleteCaptureOwnership(c,buffers,s.time,s.sequence);
        char line[200]; snprintf(line,sizeof(line),"EOS pages=%u bytes=%llu duration=%.6f capture_seconds=%.3f",
          c.stream.pages,static_cast<unsigned long long>(c.bytes),c.stream.Duration(),c.finished-c.born); Log(LogKind::Diagnostic,line);
        ready.push_back(std::move(found->second)); active.erase(found);
    }
}
static void ProcessAacCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    auto found=active.find(s.context);
    if(s.kind==AudioEventKind::AacBegin) {
        if(found==active.end()&&active.size()>=16){Log(LogKind::Failure,"AAC decoder limit reached; stream ignored");return;}
        auto capture=std::make_unique<Capture>();capture->aac=true;capture->born=s.time;
        if(found!=active.end())buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::Reinitialized);
        BeginCaptureOwnership(*capture,buffers,ownership::DecoderCodec::Aac,s.context,0,s.time,s.epoch,s.sequence);
        active[s.context]=std::move(capture);return;
    }
    if(found==active.end()||!found->second->aac)return;
    if(s.kind==AudioEventKind::AacDestroy||s.kind==AudioEventKind::AacError) {
        buffers.RecordLostActive(found->second.get(),s.kind==AudioEventKind::AacError?
            ownership::CaptureAbortReason::DecoderError:ownership::CaptureAbortReason::InvalidData);active.erase(found);
        if(s.kind==AudioEventKind::AacError)Log(LogKind::Failure,"AAC decoder input rejected; stream discarded");
        return;
    }
    auto& c=*found->second;
    auto reserved=[&] {
        size_t total=publishing_reserved.load();
        for(const auto& entry:active)total+=entry.second->Capacity();
        for(const auto& entry:buffers.ogg_active)total+=entry.second->Capacity();
        for(const auto& entry:ready)total+=entry->Capacity();
        return total;
    };
    const auto used=reserved();
    if(!c.spac.MemoryUsage()&&(used>=memory_limit||memory_limit-used<65536+64)) {
        buffers.RecordLostActive(found->second.get());active.erase(found);Log(LogKind::Failure,"AAC capture memory limit; stream discarded");return;
    }
    const auto result=c.spac.Push(s.bytes,s.length);
    if(result==AacParse::Invalid||result==AacParse::Unsupported) {
        buffers.RecordLostActive(found->second.get());active.erase(found);Log(LogKind::Failure,result==AacParse::Unsupported?
            "unsupported SpAC AAC configuration; stream discarded":"AAC page checksum or frame gap; stream discarded");return;
    }
    const auto allocated=reserved();
    if(!c.data.Append(s.bytes,s.length,allocated<memory_limit?memory_limit-allocated:0)) {
        buffers.RecordLostActive(found->second.get());active.erase(found);Log(LogKind::Failure,"AAC capture memory limit; stream discarded");return;
    }
    c.bytes+=s.length;
    if(result==AacParse::Complete) {
        c.finished=s.time;
        CompleteCaptureOwnership(c,buffers,s.time,s.sequence);
        char line[180];snprintf(line,sizeof(line),"AAC COMPLETE source_bytes=%llu rate=%u channels=%u samples=%u priming=%u duration=%.6f",
            static_cast<unsigned long long>(c.bytes),c.spac.info.rate,c.spac.info.channels,c.spac.info.total_samples,c.spac.info.priming,c.Duration());Log(LogKind::Diagnostic,line);
        ready.push_back(std::move(found->second));active.erase(found);
    }
}
static bool CompleteMp3Capture(std::map<uintptr_t,std::unique_ptr<Capture>>::iterator found,
                               CaptureBuffers& buffers,double time) {
    auto& active=buffers.active;auto& capture=*found->second;Mp3Info info;std::string error;
    if(!InspectMp3Source(capture.data,info,error))return false;
    capture.mp3_info=info;capture.finished=capture.last_input?capture.last_input:time;
    if(capture.finished<=capture.born)capture.finished=time;
    CompleteCaptureOwnership(capture,buffers,capture.finished);
    ReleaseMp3Probe(found->first,capture.capture_epoch);
    buffers.ready.push_back(std::move(found->second));active.erase(found);return true;
}
static void ProcessMp3Capture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto found=active.find(s.context);
    if(s.kind==AudioEventKind::Mp3Begin) {
        if(found==active.end()&&active.size()>=16){Log(LogKind::Failure,"MP3 decoder limit reached; stream ignored");return;}
        auto capture=std::make_unique<Capture>();capture->mp3=true;capture->born=s.time;
        capture->capture_epoch=s.epoch;capture->capture_context=s.context;
        if(found!=active.end())buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::Reinitialized);
        BeginCaptureOwnership(*capture,buffers,ownership::DecoderCodec::Mp3,s.context,0,s.time,s.epoch,s.sequence);
        active[s.context]=std::move(capture);return;
    }
    if(found==active.end()||!found->second->mp3)return;
    if(s.kind!=AudioEventKind::Mp3Bytes)return;
    size_t reserved=publishing_reserved.load();
    for(const auto& entry:active)reserved+=entry.second->Capacity();
    for(const auto& entry:buffers.ogg_active)reserved+=entry.second->Capacity();
    for(const auto& entry:buffers.ready)reserved+=entry->Capacity();
    const auto available=reserved<memory_limit?memory_limit-reserved:0;
    if(!found->second->data.Append(s.bytes,s.length,available)) {
        buffers.RecordLostActive(found->second.get(),ownership::CaptureAbortReason::MemoryLimit);
        active.erase(found);Log(LogKind::Failure,"MP3 capture memory limit; stream discarded");return;
    }
    found->second->bytes+=s.length;found->second->last_input=s.time;
}
static bool FinalizeBoundMp3Capture(CaptureBuffers& buffers,const Listen& listen,
                                    const Media& media,double time) {
    if(media.spotify_uri.rfind("spotify:episode:",0)!=0||!listen.generation)return false;
    auto match=buffers.active.end();
    for(auto it=buffers.active.begin();it!=buffers.active.end();++it) {
        auto& capture=*it->second;if(!capture.mp3||capture.ambiguous||!capture.capture_token)continue;
        if(!buffers.ownership_tracker)continue;
        const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
        if(binding.state!=ownership::OwnershipState::Bound||binding.listen_generation!=listen.generation)continue;
        if(match!=buffers.active.end()) {capture.ambiguous=true;match->second->ambiguous=true;return false;}
        match=it;
    }
    if(match==buffers.active.end())return false;
    Mp3Info info;std::string error;
    if(!InspectMp3Source(match->second->data,info,error)||std::fabs(info.Duration()-media.duration)>1.0)return false;
    match->second->mp3_info=info;match->second->listen_generation=listen.generation;
    return CompleteMp3Capture(match,buffers,time);
}
static void AbortBoundMp3Capture(CaptureBuffers& buffers,uint64_t generation,
                                 ownership::CaptureAbortReason reason) {
    if(!generation||!buffers.ownership_tracker)return;
    for(auto it=buffers.active.begin();it!=buffers.active.end();) {
        auto& capture=*it->second;
        if(!capture.mp3||!capture.capture_token||
           buffers.ownership_tracker->Binding(capture.capture_token).listen_generation!=generation) {++it;continue;}
        buffers.RecordLostActive(it->second.get(),reason);it=buffers.active.erase(it);
    }
    for(auto& capture:buffers.ready)
        if(capture->mp3&&capture->listen_generation==generation)capture->ambiguous=true;
}
static void ProcessCaptureEvent(const Slot& s,CaptureBuffers& buffers,PlaybackQualityTracker& quality,
                                bool observing_quality,bool enabled,unsigned epoch) {
    if(observing_quality) ObserveCodecQuality(s,quality);
    if(!enabled || !s.capture || s.epoch!=epoch) return;
    if(s.kind>=AudioEventKind::Mp3Begin)ProcessMp3Capture(s,buffers);
    else if(s.kind>=AudioEventKind::AacBegin)ProcessAacCapture(s,buffers);
    else if(s.kind>=AudioEventKind::FlacBegin) ProcessFlacCapture(s,buffers);
    else ProcessOggCapture(s,buffers);
}
static void LogCompletedListen(const Heard& heard) {
    if(!DebugLoggingEnabled())return;
    char line[200];snprintf(line,sizeof(line),
        "listen coverage completed generation=%llu elapsed=%.3f duration=%.3f source=%s",
        static_cast<unsigned long long>(heard.generation),heard.finish-heard.start,heard.media.duration,
        heard.media.source==MediaSource::Spotify?"spotify":"windows");
    Log(LogKind::Diagnostic,line);
}
static void RecordCompletedListen(const Media& media,const Listen& listen,std::vector<Heard>& heard,
                                  CaptureBuffers& buffers,RecentPlaybackHistory* recent) {
    auto completed=listen;completed.generation=listen.completed_generation;
    FinalizeBoundMp3Capture(buffers,completed,media,listen.completed_time);
    heard.push_back({media,listen.completed_start,listen.completed_time,listen.completed_generation});
    if(recent)recent->Complete(heard.back());
    LogCompletedListen(heard.back());
}
static void AssociateListen(Media media,Listen& listen,Media& current,std::vector<Heard>& heard,
                            CaptureBuffers& buffers,double time,RecentPlaybackHistory* recent=nullptr) {
    const auto previous_generation=listen.generation;
    bool was_eligible=listen.eligible;
    double prior_position=listen.last_position,prior_duration=listen.duration,prior_time=listen.last_time;
    if(!listen.identity.empty() && current.source!=media.source) {
        // Restart current coverage without revoking independently completed
        // listens or their unresolved decoder lifecycles.
        listen.eligible=false;
        Log(LogKind::Diagnostic,"playback source changed; listening coverage restarted");
    }
    const auto configured_speed=CaptureSpeed();
    const auto effective_speed=configured_speed>1?configured_speed:media.playback_speed;
    if(media.explicit_skip||media.explicit_control||media.prior_control)buffers.confirmed_transition_playback=0;
    if(media.prior_control)listen.eligible=false;
    const bool reconcile_interstitial=PrepareUnidentifiedSuccessor(buffers,current,media);
    const bool excluded_interlude=ExcludeUnboundMp3Interludes(buffers,media);
    const bool delayed_start=BoundCaptureProvesInterludeSuccessor(
        buffers,media,excluded_interlude,reconcile_interstitial);
    std::string done=listen.Observe(media.ListenKey(),media.position,media.duration,media.playing,time,effective_speed);
    if(media.explicit_skip||media.explicit_control) {
        listen.eligible=false;listen.pending_start=false;
    }
    if(delayed_start&&listen.pending_start) {
        listen.eligible=true;listen.pending_start=false;
        if(DebugLoggingEnabled())Log(LogKind::Diagnostic,
            "decoder binding recovered track start hidden by metadata gap");
    }
    buffers.confirmed_transition_playback=0;
    if(previous_generation!=listen.generation||!listen.eligible)
        buffers.ownership_hold_generation=0;
    if(was_eligible && !listen.eligible && done.empty()) {
        char line[300]; snprintf(line,sizeof(line),"listen invalidated old=%.6f/%.6f new=%.6f/%.6f delta=%.3f playing=%d",
            prior_position,prior_duration,media.position,media.duration,time-prior_time,media.playing); Log(LogKind::Diagnostic,line);
        AbortBoundMp3Capture(buffers,previous_generation,ownership::CaptureAbortReason::InvalidData);
    }
    if(!done.empty())RecordCompletedListen(current,listen,heard,buffers,recent);
    if(previous_generation!=listen.generation && listen.eligible) LogEvent(LogKind::Activity,"started",Utf8(media.artist)+" - "+Utf8(media.title));
    if(was_eligible && !listen.eligible && done.empty()) LogEvent(LogKind::Activity,"skipped",Utf8(current.artist)+" - "+Utf8(current.title)+" (incomplete listen)");
    if(listen.transient) Log(LogKind::Diagnostic,"timeline reset at natural end; waiting for matching title");
    else {current=std::move(media);if(recent)recent->Observe(current,listen);}
}
static Capture* BoundCompleteCaptureForListen(CaptureBuffers& buffers,const Listen& listen,
                                              const Media& current,
                                              ownership::OwnershipBinding& binding) {
    binding={};Capture* match=nullptr;
    if(!buffers.ownership_tracker||!listen.generation)return nullptr;
    for(auto& item:buffers.ready) {
        auto& capture=*item;
        if(!capture.decoder_owner||!capture.capture_token||capture.ambiguous)continue;
        const auto candidate=buffers.ownership_tracker->Binding(capture.capture_token);
        const bool durable_mp3=capture.mp3&&capture.listen_generation==listen.generation&&
            candidate.listen_generation==listen.generation&&candidate.playback_token;
        if((candidate.state!=ownership::OwnershipState::Bound&&!durable_mp3)||
           candidate.listen_generation!=listen.generation)continue;
        if(std::fabs(capture.Duration()-current.duration)>1.0||
           CachedFinalValidation(capture)!=FinalCaptureValidation::Valid)continue;
        if(match)return nullptr; // More than one bound complete capture is contradictory.
        match=&capture;binding=candidate;
    }
    return match;
}
static Capture* UniqueCompleteUnownedAacForListen(CaptureBuffers& buffers,const Listen& listen,
                                                   const Media& current,double time) {
    if(listen.capture_speed<=1||!listen.generation)return nullptr;
    Capture* match=nullptr;
    for(auto& item:buffers.ready) {
        auto& capture=*item;
        if(!capture.aac||capture.decoder_owner||capture.ambiguous||!capture.spac.Complete()||
           capture.finished<=capture.born||capture.finished>time||
           std::fabs(capture.Duration()-current.duration)>1.0||
           CachedFinalValidation(capture)!=FinalCaptureValidation::Valid)continue;
        if(match)return nullptr;
        match=&capture;
    }
    if(!match)return nullptr;
    const auto overlaps=[&](const Capture& candidate) {
        return &candidate!=match&&candidate.born<=match->finished&&
               (!candidate.finished||candidate.finished>=match->born);
    };
    for(const auto& item:buffers.active)if(overlaps(*item.second))return nullptr;
    for(const auto& item:buffers.ogg_active)if(overlaps(*item.second))return nullptr;
    for(const auto& item:buffers.ready)if(overlaps(*item))return nullptr;
    Heard pending{current,listen.start_time,match->finished,listen.generation};
    if(pending.start<=buffers.lost_capture_start_through||buffers.LostActiveMatches(pending))return nullptr;
    return match;
}
static bool FinishAppTransition(const Media& next,Listen& listen,const Media& current,std::vector<Heard>& heard,CaptureBuffers& buffers,double time,RecentPlaybackHistory* recent=nullptr) {
    if(listen.identity.empty()) {
        buffers.ownership_hold_generation=0;ClearPendingNaturalTransition(buffers);
        buffers.confirmed_transition_playback=0;
        return false;
    }
    if(next.explicit_skip||next.explicit_control) {
        buffers.ownership_hold_generation=0;ClearPendingNaturalTransition(buffers);
        buffers.confirmed_transition_playback=0;
        AbortBoundMp3Capture(buffers,listen.generation,ownership::CaptureAbortReason::InvalidData);
        listen.identity.clear();listen.eligible=false;listen.playing=false;return false;
    }
    const bool confirmed_successor=next.natural_transition&&next.control_monitoring&&
        !next.playback_instance.empty()&&!current.playback_instance.empty()&&
        next.playback_instance!=current.playback_instance&&listen.playing;
    buffers.confirmed_transition_playback=confirmed_successor&&buffers.ownership_tracker?
        buffers.ownership_tracker->PlaybackToken(next.playback_instance):0;
    // A monitored transition without an explicit control is not itself proof
    // of either completion or rejection. Keep the old listen intact until a
    // Ready snapshot identifies whether playback resumed or a successor began;
    // all ordinary completion and integrity checks still run at that stage.
    const bool observed_successor_stage=!next.natural_transition&&
        !next.playback_instance.empty()&&next.playback_instance!=current.playback_instance&&
        buffers.ownership_hold_generation==listen.generation;
    const bool awaiting_ready=next.transition_pending||observed_successor_stage;
    if(awaiting_ready&&next.control_monitoring&&listen.eligible&&listen.playing) {
        buffers.pending_natural_generation=listen.generation;
        buffers.pending_natural_time=time;
        buffers.pending_natural_playback_instance=
            current.playback_instance.empty()||
            next.playback_instance!=current.playback_instance?next.playback_instance:"";
        buffers.pending_natural_listen=listen;
        return true;
    }
    FinalizeBoundMp3Capture(buffers,listen,current,time);
    const bool same=listen.identity==next.ListenKey();
    ownership::OwnershipBinding owned_binding{};
    auto* owned_complete=BoundCompleteCaptureForListen(buffers,listen,current,owned_binding);
    auto* unowned_aac=UniqueCompleteUnownedAacForListen(buffers,listen,current,time);
    const auto next_playback=buffers.ownership_tracker?
        buffers.ownership_tracker->PlaybackToken(next.playback_instance):0;
    const bool same_owned_instance=owned_complete&&next_playback&&
        next_playback==owned_binding.playback_token;
    const bool new_owned_instance=owned_complete&&next_playback&&
        next_playback!=owned_binding.playback_token;
    const bool windows_natural_bridge=current.source==MediaSource::Windows&&owned_complete&&
        next.natural_transition&&next.control_monitoring&&listen.playing&&next_playback&&
        owned_binding.playback_token&&next_playback!=owned_binding.playback_token;
    const bool complete_flac_decode=owned_complete&&owned_complete->lossless&&
        owned_complete->metadata&&owned_complete->coverage.Complete(owned_complete->flac);
    const bool complete_ogg_decode=owned_complete&&listen.capture_speed>1&&
        !owned_complete->lossless&&!owned_complete->aac&&owned_complete->stream.complete&&
        CachedFinalValidation(*owned_complete)==FinalCaptureValidation::Valid;
    const bool complete_aac_decode=owned_complete&&listen.capture_speed>1&&owned_complete->aac&&
        owned_complete->spac.Complete()&&
        CachedFinalValidation(*owned_complete)==FinalCaptureValidation::Valid;
    const bool complete_mp3_decode=owned_complete&&owned_complete->mp3&&
        CachedFinalValidation(*owned_complete)==FinalCaptureValidation::Valid;
    // Client buffering first repeats the old playbackId. Preserve the eligible
    // listen until a positively different instance arrives; the complete file
    // and decoder binding remain cached during this short metadata gap.
    const bool continuous_position=next.position<0||std::fabs(next.position-listen.last_position)<=1.5;
    if(same&&(same_owned_instance||unowned_aac)&&next.control_monitoring&&continuous_position&&
       listen.eligible&&listen.playing) {
        buffers.ownership_hold_generation=listen.generation;return true;
    }
    const bool ownership_hold_proven=buffers.ownership_hold_generation==listen.generation;
    // The app's transition timestamp, rather than time spent buffering, closes
    // the old listen. The new track cannot accrue coverage until it is playing.
    auto ending=listen;
    std::string done;
    if(!same)done=ending.Observe(next.ListenKey(),0,current.duration,false,time,ending.capture_speed);
    else if(next.position>=current.duration)done=ending.Observe(next.ListenKey(),next.position,current.duration,false,time,ending.capture_speed);
    else if(next.position>=0&&next.position<=1.5) {
        ending.Observe(next.ListenKey(),next.position,current.duration,true,time,ending.capture_speed);
        if(ending.transient && listen.last_position+(time-listen.last_time)*listen.capture_speed>=listen.duration-0.1) {
            buffers.ownership_hold_generation=0;
            listen=std::move(ending);return true; // Confirm a same-URI repeat on fresh unbuffered progress.
        }
    }
    if(!done.empty()&&(current.source==MediaSource::Spotify||windows_natural_bridge)) {
        RecordCompletedListen(current,ending,heard,buffers,recent);
    } else if(listen.eligible) {
        const bool changed_playback_instance=!current.playback_instance.empty()&&
            !next.playback_instance.empty()&&current.playback_instance!=next.playback_instance;
        const bool unowned_aac_transition=unowned_aac&&changed_playback_instance&&
            ownership_hold_proven&&next.control_monitoring&&listen.playing;
        Heard pending{current,listen.start_time,
            unowned_aac_transition?unowned_aac->finished:time,listen.generation};
        const auto compatible=[&](const auto& capture){return CaptureTimingMatchesListen(*capture,pending)&&
            CaptureDurationCouldMatch(*capture,pending);};
        const auto exactly_bound=[&](const Capture& capture) {
            if(!buffers.ownership_tracker||!capture.capture_token)return false;
            const auto binding=buffers.ownership_tracker->Binding(capture.capture_token);
            return binding.state==ownership::OwnershipState::Bound&&
                binding.listen_generation==listen.generation;
        };
        size_t candidates=0,complete_ready=0,bound_active=0,unowned_candidates=0;
        for(const auto& capture:buffers.active)if(compatible(capture.second)) {
            ++candidates;bound_active+=exactly_bound(*capture.second);
            unowned_candidates+=!capture.second->decoder_owner||!capture.second->capture_token;
        }
        for(const auto& capture:buffers.ogg_active)if(compatible(capture.second)) {
            ++candidates;bound_active+=exactly_bound(*capture.second);
            unowned_candidates+=!capture.second->decoder_owner||!capture.second->capture_token;
        }
        for(const auto& capture:buffers.ready)if(compatible(capture)) {
            ++candidates;
            unowned_candidates+=!capture->decoder_owner||!capture->capture_token;
            if(std::fabs(capture->Duration()-current.duration)<=1.0&&
               CachedFinalValidation(*capture)==FinalCaptureValidation::Valid)++complete_ready;
        }
        const bool natural=next.natural_transition&&!next.explicit_skip&&!same&&
            !current.playback_instance.empty()&&!next.playback_instance.empty()&&
            current.playback_instance!=next.playback_instance&&listen.playing;
        if((new_owned_instance&&ownership_hold_proven&&next.control_monitoring&&
            (complete_flac_decode||complete_ogg_decode||complete_aac_decode||complete_mp3_decode)&&listen.playing)||
           (windows_natural_bridge&&
            (complete_flac_decode||complete_ogg_decode||complete_aac_decode||complete_mp3_decode))||
           (unowned_aac_transition&&candidates==1&&complete_ready==1)||
           (natural&&candidates==1&&
            (complete_ready==1||bound_active==1||unowned_candidates==1))) {
            pending.provisional=true;heard.push_back(std::move(pending));
            if(recent)recent->Observe(current,listen);
            Log(LogKind::Diagnostic,"incomplete app transition quarantined for final capture validation");
        } else LogEvent(LogKind::Activity,"skipped",Utf8(current.artist)+" - "+Utf8(current.title)+" (incomplete app transition)");
    }
    buffers.ownership_hold_generation=0;ClearPendingNaturalTransition(buffers);
    listen.identity.clear();listen.eligible=false;listen.playing=false;
    return false;
}
static bool FinishPendingNaturalTransition(const Media& next,Listen& listen,const Media& current,
                                           std::vector<Heard>& heard,CaptureBuffers& buffers,
                                           RecentPlaybackHistory* recent=nullptr) {
    if(!buffers.pending_natural_generation)return false;
    if(next.explicit_skip||next.explicit_control||next.prior_control) {
        ClearPendingNaturalTransition(buffers);buffers.confirmed_transition_playback=0;
        listen.eligible=false;listen.playing=false;return false;
    }
    if(!listen.eligible||buffers.pending_natural_generation!=listen.generation) {
        ClearPendingNaturalTransition(buffers);return false;
    }
    if(next.playback_instance.empty())return false;
    if(!buffers.pending_natural_playback_instance.empty()&&
       next.playback_instance!=buffers.pending_natural_playback_instance) {
        ClearPendingNaturalTransition(buffers);buffers.confirmed_transition_playback=0;
        listen.eligible=false;listen.playing=false;return false;
    }
    if(buffers.pending_natural_playback_instance.empty()&&
       !current.playback_instance.empty()&&
       next.playback_instance==current.playback_instance)return false;
    if(next.ListenKey()==listen.identity)return false;
    auto transition=next;transition.natural_transition=true;transition.control_monitoring=true;
    transition.explicit_skip=false;transition.explicit_control=false;
    const auto transition_time=buffers.pending_natural_time;
    auto pending=std::move(buffers.pending_natural_listen);
    FinishAppTransition(transition,pending,current,heard,buffers,transition_time,recent);
    listen=std::move(pending);
    return true;
}
static bool CaptureOwnershipCouldMatch(const Capture& capture,const Heard& heard,
                                       const ownership::DecoderOwnershipTracker* tracker) {
    if(!tracker||!capture.decoder_owner||!capture.capture_token||
       heard.media.playback_instance.empty())return true;
    const auto binding=tracker->Binding(capture.capture_token);
    if(binding.state==ownership::OwnershipState::Bound)
        return binding.listen_generation&&binding.listen_generation==heard.generation;
    return binding.state==ownership::OwnershipState::AwaitingOutput||
           binding.state==ownership::OwnershipState::AwaitingPlaybackInstance;
}
static bool CaptureEvidenceMatchesListen(const Capture& capture,const Heard& heard,
    const ownership::DecoderOwnershipTracker* tracker=nullptr) {
    if(tracker&&capture.decoder_owner&&capture.capture_token&&
       !heard.media.playback_instance.empty()) {
        const auto binding=tracker->Binding(capture.capture_token);
        const bool durable_mp3=capture.mp3&&capture.listen_generation==heard.generation&&
            binding.listen_generation==heard.generation&&binding.playback_token;
        if((binding.state!=ownership::OwnershipState::Bound&&!durable_mp3)||!binding.listen_generation||
           binding.listen_generation!=heard.generation)return false;
    }
    return CaptureTimingMatchesListen(capture,heard) &&
           std::fabs(capture.Duration()-heard.media.duration)<=1.0 &&
           (!heard.provisional||CachedFinalValidation(capture)==FinalCaptureValidation::Valid);
}
static bool CaptureMatchesListen(const Capture& capture,const Heard& heard,
    const ownership::DecoderOwnershipTracker* tracker=nullptr) {
    return !capture.ambiguous&&!heard.ambiguous&&CaptureEvidenceMatchesListen(capture,heard,tracker);
}
[[maybe_unused]] static std::string AssociationFailure(const Heard& heard,const CaptureBuffers& buffers,size_t matches) {
    size_t timing_ready=0,duration_ready=0,timing_active=0,flac_active=0,ogg_active=0;
    for(const auto& c:buffers.ready) {
        timing_ready+=CaptureTimingMatchesListen(*c,heard);
        duration_ready+=std::fabs(c->Duration()-heard.media.duration)<=1.0;
    }
    for(const auto& entry:buffers.active) {
        const auto& c=*entry.second;
        if(CaptureTimingMatchesListen(c,heard)) {
            ++timing_active;
            if(c.lossless)++flac_active;else ++ogg_active;
        }
    }
    for(const auto& entry:buffers.ogg_active) {
        const auto& c=*entry.second;
        if(CaptureTimingMatchesListen(c,heard)){++timing_active;++ogg_active;}
    }
    char counts[260];
    snprintf(counts,sizeof(counts)," | matches=%zu ready=%zu timing_ready=%zu duration_ready=%zu active=%zu timing_active=%zu (FLAC=%zu Ogg=%zu) expected=%.3fs",
        matches,buffers.ready.size(),timing_ready,duration_ready,buffers.active.size()+buffers.ogg_active.size(),timing_active,flac_active,ogg_active,heard.media.duration);
    return std::string("completed listen discarded: ")+
        (matches>1?"ambiguous completed captures":"no matching complete capture")+counts+
        " | "+Utf8(heard.media.artist)+" - "+Utf8(heard.media.title);
}
static void SchedulePublications(std::vector<Heard>& heard,CaptureBuffers& buffers,unsigned epoch,double now,RecentPlaybackHistory* recent=nullptr) {
    (void)now;
    auto& ready=buffers.ready;
    if(recent)for(auto& pending:heard)recent->Refresh(pending);
    const auto active_alternative=[&](const Heard& h) {
        for(const auto& c:buffers.active)
            if(CaptureOwnershipCouldMatch(*c.second,h,buffers.ownership_tracker)&&
               CaptureTimingMatchesListen(*c.second,h)&&CaptureDurationCouldMatch(*c.second,h))return true;
        for(const auto& c:buffers.ogg_active)
            if(CaptureOwnershipCouldMatch(*c.second,h,buffers.ownership_tracker)&&
               CaptureTimingMatchesListen(*c.second,h)&&CaptureDurationCouldMatch(*c.second,h))return true;
        return false;
    };
    // Assess every alternative before expiry/pressure can remove evidence.
    std::vector<size_t> listen_matches(heard.size()),capture_matches(ready.size());
    std::vector<std::pair<size_t,size_t>> links;
    for(size_t h=0;h<heard.size();++h)for(size_t c=0;c<ready.size();++c)
        if(CaptureEvidenceMatchesListen(*ready[c],heard[h],buffers.ownership_tracker))
            {++listen_matches[h];++capture_matches[c];links.push_back({h,c});}
    for(const auto& link:links) {
        auto& h=heard[link.first];auto& c=*ready[link.second];
        if(listen_matches[link.first]>1||capture_matches[link.second]>1||h.ambiguous||c.ambiguous)
            h.ambiguous=c.ambiguous=true;
    }
    for(auto h=heard.begin();h!=heard.end();) {
        if(h->start<=buffers.lost_capture_start_through||buffers.LostActiveMatches(*h))h->ambiguous=true;
        size_t match=ready.size(),matches=0;
        for(size_t i=0;i<ready.size();i++) {
            Capture& c=*ready[i];
            if(c.born<=buffers.lost_listen_born_through)c.ambiguous=true;
            if(CaptureMatchesListen(c,*h,buffers.ownership_tracker)) { match=i; ++matches; }
        }
        // Matching is one-to-one across recent completed listens as well as
        // decoder candidates. A buffer compatible with two songs is ambiguous.
        if(matches==1)for(auto other=heard.begin();other!=heard.end();++other)
            if(other!=h&&CaptureMatchesListen(*ready[match],*other,buffers.ownership_tracker)){matches=2;break;}
        // EOS order cannot prove ownership. Remember the unresolved alternative
        // so corruption or cleanup cannot later manufacture uniqueness.
        if(matches&&active_alternative(*h)) {
            h->unresolved_alternative=true;++h;continue;
        }
        if(h->unresolved_alternative) {
            h->ambiguous=true;
            for(auto& candidate:ready)
                if(CaptureEvidenceMatchesListen(*candidate,*h,buffers.ownership_tracker))candidate->ambiguous=true;
        }
        if(h->ambiguous) {
            LogEvent(LogKind::Failure,"failed","ambiguous association rejected; removing alternatives cannot establish ownership");h=heard.erase(h);continue;
        }
        if(matches==1) {
            SaveJob job;job.reserved=ready[match]->Capacity();job.epoch=epoch;
            job.capture=std::move(ready[match]);job.heard=*h;job.capture->listen_generation=h->generation;
            publishing_reserved.fetch_add(job.reserved);
            if(saves.Push(std::move(job)))SetEvent(save_event);
            else {
                publishing_reserved.fetch_sub(job.reserved);
                ready[match]=std::move(job.capture);
                if(!ready[match]->publication_deferred) {
                    ready[match]->publication_deferred=true;
                    Log(LogKind::Warning,"publication queue full; complete capture retained for retry");
                }
                ++h;continue;
            }
            ready.erase(ready.begin()+match); h=heard.erase(h);
        } else ++h;
    }
    while(heard.size()>5) {
        buffers.lost_listen_born_through=std::max(buffers.lost_listen_born_through,heard.front().start+3);
        LogEvent(LogKind::Failure,"failed","unmatched completed listen evicted from five-song history");heard.erase(heard.begin());
    }
    while(ready.size()>5) {
        buffers.lost_capture_start_through=std::max(buffers.lost_capture_start_through,ready.front()->born+20);
        CheckBeforeRelease(*ready.front(),"five-song capacity",buffers.ownership_tracker);ready.erase(ready.begin());
    }
}
struct MediaWarningState {double since=-1;bool reported=false;};
static void ReportMediaUnavailable(const CaptureBuffers& buffers,bool unavailable,
                                   const MediaDiagnostic& diagnostic,double now,MediaWarningState& warning) {
    // App state can settle just after the first decoder pages arrive. The delay
    // controls warning noise only; it never changes capture or listen state.
    if(!unavailable || ActiveInterstitialReason(diagnostic.reason) ||
       (buffers.active.empty()&&buffers.ogg_active.empty()) ||
       std::strcmp(diagnostic.reason,"client-buffering-transition")==0) {
        warning={};return;
    }
    if(warning.since<0){warning.since=now;return;}
    if(warning.reported || now-warning.since<3)return;
    warning.reported=true;
    const auto message=std::string("playback verification unavailable while audio is being captured; ")+MediaRecoveryHint(diagnostic);
    Log(LogKind::Warning,message.c_str());
}
static void ReportUnverifiedCaptures(CaptureBuffers& buffers,bool unavailable,
                                     const MediaDiagnostic& diagnostic,double now) {
    if(!unavailable||ActiveInterstitialReason(diagnostic.reason))return;
    for(auto& capture:buffers.ready) {
        if(capture->unverified_reported || now-capture->finished<3)continue;
        capture->unverified_reported=true;
        const char* codec=capture->mp3?"MP3":capture->aac?"AAC":capture->lossless?"FLAC":"Ogg";
        const auto message=std::string("complete ")+codec+" capture not saved: playback verification unavailable; "+MediaRecoveryHint(diagnostic);
        Log(LogKind::Failure,message.c_str());
    }
}
static DWORD WINAPI Worker(LPVOID) {
    LONG overflow=0;
    ownership::DecoderOwnershipTracker ownership_tracker;
    CaptureBuffers buffers;buffers.ownership_tracker=&ownership_tracker;
    auto& active=buffers.active;auto& ready=buffers.ready;
    std::vector<Heard> heard;RecentPlaybackHistory recent;
    Listen listen; PlaybackSnapshotReader reader; Media current; PlaybackQualityTracker quality; std::string client_quality;
    uint64_t media_sequence=0;
    PlaybackDiagnostics diagnostics;
    MediaDiagnostic media_diagnostic;MediaDiagnosticReporter media_reporter;
    double last_media=Now(),minimum_media_time=last_media,next_timing=0;
    bool observer_started=false,media_unavailable=false;MediaWarningState media_warning;
    std::string ownership_playback_uri,ownership_transition_uri;
    uint64_t ownership_playback_token=0,ownership_listen_generation=0;
    unsigned generation=~0u,epoch=~0u; bool enabled=false;
    try {
        while(workers_running.load(std::memory_order_acquire)) {
            NoteCaptureProgress();
            double now=Now();
            DrainTiming(now,next_timing);
            if(decoded_fallback_requested.exchange(false,std::memory_order_acq_rel)) {
                const auto reason=DecodedFallbackReason(decoded_fallback_reason.exchange(
                    unsigned(DecodedFallbackReason::None),std::memory_order_acq_rel));
                const char* detail="unknown";
                switch(reason) {
                    case DecodedFallbackReason::MissingSpan:detail="missing-span";break;
                    case DecodedFallbackReason::DecodedSpan:detail="decoded-span";break;
                    case DecodedFallbackReason::EncodedSpan:detail="encoded-span";break;
                    case DecodedFallbackReason::MissingOwner:detail="missing-owner";break;
                    case DecodedFallbackReason::OddSamples:detail="odd-samples";break;
                    case DecodedFallbackReason::UnsupportedSpeed:detail="unsupported-speed";break;
                    default:break;
                }
                SetCaptureAccelerationAvailable(false);
                SetCaptureSpeed(1);
                char message[128];snprintf(message,sizeof(message),
                    "decoded audio layout became invalid (%s); capture speed fell back to 1x",detail);
                Log(LogKind::Warning,message);
            }
            auto preferences=GetSettings();
            if(preferences.generation!=generation) {
                bool initial=generation==~0u;
                generation=preferences.generation;
                reader.SetWindowsFallback(preferences.windows_playback_fallback);
                output=preferences.root; memory_limit=size_t(preferences.max_buffered_mib)*1024*1024;
                bool capture=preferences.downloads && (preferences.ogg || preferences.flac || preferences.aac || preferences.podcasts);
                if(capture!=enabled || initial || preferences.capture_epoch!=epoch) {
                    for(auto& c:buffers.active)buffers.RecordLostActive(c.second.get(),ownership::CaptureAbortReason::SettingsChange);
                    for(auto& c:buffers.ogg_active)buffers.RecordLostActive(c.second.get(),ownership::CaptureAbortReason::SettingsChange);
                    for(auto& c:ready)CheckBeforeRelease(*c,"capture settings changed",buffers.ownership_tracker);
                    buffers={};buffers.ownership_tracker=&ownership_tracker;
                    heard.clear();recent.Clear(); listen={}; current={};
                    last_media=minimum_media_time=now;media_unavailable=false;media_warning={};diagnostics.known=false;CancelFilenameChecks();
                    enabled=capture;
                    ownership_tracker.Reset(preferences.capture_epoch);
                    ResetMp3Probes();
                    ownership_output_logged={};ownership_output_logged_count=ownership_output_logged_next=0;
                    ownership_playback_uri.clear();
                    ownership_transition_uri.clear();
                    ownership_playback_token=ownership_listen_generation=0;
                    LogEvent(LogKind::Activity,"capture",enabled ? "enabled" : "disabled");
                }
                epoch=preferences.capture_epoch;
            }
            if(!enabled && !quality_enabled.load(std::memory_order_relaxed)) {
                reader.Stop();observer_started=false;
                PublishPlaybackQuality({});
                PublishSongDestination(0,L"",{},false,0);
                Slot discard; while(Pop(discard)) {}
                DrainDecodedActivity(ownership_tracker,epoch);
                WaitForSingleObject(event,200); continue;
            }
            if(dropped!=overflow) {
                overflow=dropped;
                for(auto& c:active)buffers.RecordLostActive(c.second.get(),ownership::CaptureAbortReason::QueueLoss);
                for(auto& c:buffers.ogg_active)buffers.RecordLostActive(c.second.get(),ownership::CaptureAbortReason::QueueLoss);
                for(auto& c:ready)c->ambiguous=true;
                ownership_tracker.ObservationLost(ownership::ObservationLoss::CaptureQueue,now);
                active.clear(); buffers.ogg_active.clear(); heard.clear(); listen.eligible=false; quality.ClearStreams();
                ResetMp3Probes();
                Log(LogKind::Failure,"capture queue overflow; active streams and listening coverage invalidated; complete captures retained for final audit");
            }
            Slot s;
            while(Pop(s)) {
                ProcessCaptureEvent(s,buffers,quality,quality_enabled.load(std::memory_order_relaxed),enabled,epoch);
                // Draining a continuously busy queue is still forward progress.
                NoteCaptureProgress();
                if(HookDiagnosticsEnabled())DrainTiming(Now(),next_timing);
            }
            DrainDecodedActivity(ownership_tracker,epoch);
            LogPlaybackIdentityMatch();
            // Start only the basic media observer. All COM/WinRT work stays on
            // its thread; this consumer only try-locks a bounded RAM snapshot.
            // A still-stopping observer must finish before another can start.
            if(!observer_started) observer_started=reader.Start();
            Media media;double observed_at=0;
            auto observation=observer_started?reader.TryRead(media,observed_at,media_sequence,&media_diagnostic):MediaRead::Pending;
            if(observation!=MediaRead::Pending && observed_at<minimum_media_time)
                observation=MediaRead::Pending; // Never reuse an earlier capture epoch.
            if(!observer_started){media_diagnostic.stage="observer-start";media_diagnostic.reason="thread-start-failed";}
            if(observation==MediaRead::Pending && Now()-last_media>3 && std::strcmp(media_diagnostic.stage,"ready")==0) {
                media_diagnostic.stage="snapshot-freshness";media_diagnostic.reason="no-fresh-observation";
            }
            if(DebugLoggingEnabled() && media_reporter.Due(media_diagnostic,Now()))Log(LogKind::Diagnostic,DescribeMediaDiagnostic(media_diagnostic).c_str());
            if(observation==MediaRead::Ready) {
                last_media=observed_at;media_unavailable=false;
                diagnostics.Observe(media,observed_at,media_sequence);
                PublishCurrentDestination(media,preferences);
                quality.Media(media.Key(),media.title,media.position,media.duration,observed_at,media.playing);
                client_quality=ReadClientPlaybackQuality(media);
                CarryBoundReadyIdentity(media,current,!ownership_transition_uri.empty());
                const auto playback_uri=media.spotify_uri;
                const bool reconcile_interstitial=
                    PrepareUnidentifiedSuccessor(buffers,current,media);
                const auto confirmed_transition=buffers.confirmed_transition_playback;
                if(enabled) {
                    FinishPendingNaturalTransition(media,listen,current,heard,buffers,&recent);
                    AssociateListen(std::move(media),listen,current,heard,buffers,observed_at,&recent);
                }
                std::array<playback_identity::Match,playback_identity::kObservationCapacity>
                    direct_matches{};
                const auto direct_match_count=SetExpectedPlaybackIdentity(
                    current,listen.generation,direct_matches);
                auto playback_token=BindDirectReadyCapture(
                    buffers,current,listen.generation,observed_at,
                    direct_matches,direct_match_count);
                if(!playback_token)playback_token=BindReadyCapture(
                    buffers,current,listen.generation,observed_at,confirmed_transition,
                    ownership_transition_uri,reconcile_interstitial);
                if(playback_token)ClearUnidentifiedSuccessor(buffers);
                CanonicalizeBoundReadyIdentity(current,listen,
                    current.spotify_uri.empty()?ownership_transition_uri:current.spotify_uri,
                    playback_token);
                RememberBoundCaptureIdentity(buffers,current,listen.generation,playback_token);
                if(playback_token)ownership_transition_uri.clear();
                if(DebugLoggingEnabled()&&playback_token&&
                   (playback_token!=ownership_playback_token||playback_uri!=ownership_playback_uri||
                    listen.generation!=ownership_listen_generation)) {
                    char line[240];snprintf(line,sizeof(line),
                        "ownership event=playback instance=%llu uri_changed=%d listen_generation=%llu mono=%.3f",
                        static_cast<unsigned long long>(playback_token),
                        !ownership_playback_uri.empty()&&ownership_playback_uri!=playback_uri,
                        static_cast<unsigned long long>(listen.generation),observed_at);
                    Log(LogKind::Diagnostic,line);
                }
                ownership_playback_uri=playback_uri;ownership_playback_token=playback_token;
                ownership_listen_generation=listen.generation;
            } else if(observation==MediaRead::Transition) {
                ownership_tracker.ObserveTransition(media.playback_instance,media.spotify_uri,
                    observed_at,media.explicit_skip||media.explicit_control,media.natural_transition);
                const auto next_token=ownership_tracker.PlaybackToken(media.playback_instance);
                if(next_token)ownership_transition_uri=media.spotify_uri;
                if(DebugLoggingEnabled()) {
                    char line[240];snprintf(line,sizeof(line),
                        "ownership event=transition from=%llu to=%llu explicit_skip=%d controlled=%d monitoring=%d natural=%d mono=%.3f",
                        static_cast<unsigned long long>(ownership_playback_token),
                        static_cast<unsigned long long>(next_token),media.explicit_skip,
                        media.explicit_control,media.control_monitoring,
                        media.natural_transition,observed_at);Log(LogKind::Diagnostic,line);
                }
                if(next_token)ownership_playback_token=next_token;
                const bool pending_repeat=enabled&&FinishAppTransition(media,listen,current,heard,buffers,observed_at,&recent);
                // Explicit buffering is never converted into Windows fallback
                // or new listening coverage. A fresh unbuffered state is needed.
                if(!pending_repeat)listen.eligible=false;
                media_unavailable=true;diagnostics.known=false;
            } else if(observation==MediaRead::Failed || Now()-last_media>3) {
                if(ActiveInterstitialReason(media_diagnostic.reason))
                    MarkUnidentifiedInterstitial(buffers,media_diagnostic.reason);
                // Absence of a fresh observation is not evidence of a skip,
                // seek, pause, ownership conflict, or lost audio. Preserve the
                // last proven stages. A later Ready snapshot must reconcile
                // actual position before any listen can complete.
                PublishSongDestination(0,L"",{},false,0);
                const auto gap_time=Now();
                quality.Media({},L"",0,0,gap_time);client_quality.clear();
                if(!media_unavailable){CancelFilenameChecks();Log(LogKind::Diagnostic,
                    "Spotify media snapshot unavailable or stale; prior capture and listen evidence retained");}
                media_unavailable=true;diagnostics.known=false;
            }
            LogOwnershipBindings(buffers);
            ReportMediaUnavailable(buffers,media_unavailable,media_diagnostic,Now(),media_warning);
            if(!enabled) {
                PublishPlaybackQuality(quality.Snapshot(client_quality));
                WaitForSingleObject(event,50);continue;
            }
            now=Now();
            PublishPlaybackQuality(quality.Snapshot(client_quality));
            // Menu settings may change concurrently with snapshot processing.
            // Apply that generation before considering any publication.
            if(GetSettings().generation!=generation) continue;
            SchedulePublications(heard,buffers,epoch,now,&recent);
            ReportUnverifiedCaptures(buffers,media_unavailable,media_diagnostic,now);
            // Bound unresolved codec lifecycles by count, not age. Capacity
            // pressure is explicit resource evidence; elapsed time is not a
            // reason to declare otherwise valid audio unrelated or invalid.
            while(active.size()>32) {
                auto oldest=std::min_element(active.begin(),active.end(),[](const auto& left,const auto& right) {
                    return left.second->born<right.second->born;
                });
                buffers.RecordLostActive(oldest->second.get(),ownership::CaptureAbortReason::MemoryLimit);
                active.erase(oldest);
            }
            while(buffers.ogg_active.size()>32) {
                auto oldest=std::min_element(buffers.ogg_active.begin(),buffers.ogg_active.end(),
                    [](const auto& left,const auto& right) {return left.second->born<right.second->born;});
                buffers.RecordLostActive(oldest->second.get(),ownership::CaptureAbortReason::MemoryLimit);
                buffers.ogg_active.erase(oldest);
            }
            WaitForSingleObject(event,50);
        }
    } catch(...) { Log(LogKind::Failure,"history worker stopped after an exception; playback remains with original parser"); }
    for(auto& capture:buffers.active)
        buffers.RecordLostActive(capture.second.get(),ownership::CaptureAbortReason::Shutdown);
    for(auto& capture:buffers.ogg_active)
        buffers.RecordLostActive(capture.second.get(),ownership::CaptureAbortReason::Shutdown);
    for(auto& capture:buffers.ready)
        CheckBeforeRelease(*capture,"worker shutdown",buffers.ownership_tracker);
    capture_progress_ms.store(0,std::memory_order_release);
    CancelFilenameChecks();
    reader.Stop(); // Nonblocking even if Windows is still servicing a COM call.
    PublishPlaybackQuality({});
    PublishSongDestination(0,L"",{},false,0);
    workers_running.store(false,std::memory_order_release);
    if(save_event) SetEvent(save_event);
    MH_DisableHook(target);
    for(auto address:flac_targets) if(address) MH_DisableHook(address);
    for(auto address:aac_targets) if(address) MH_DisableHook(address);
    if(decoded_sink_target)MH_DisableHook(decoded_sink_target);
    if(playback_pump_target)MH_DisableHook(playback_pump_target);
    SetCaptureAccelerationAvailable(false);
    return 0;
}
}

void StartAudioHistory(HMODULE spotify_module,HMODULE proxy) {
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!audio_init.TryBegin(now)) return;
    (void)proxy;
    auto preferences=GetSettings(); output=preferences.root;
    quality_enabled.store(preferences.menu,std::memory_order_relaxed);
    StartSongDestinationWorker(preferences.menu);
    if(output.empty()) { Log(LogKind::Failure,"save location unavailable; history initialization will retry"); audio_init.Retry(now); return; }
    spotify::HookTargets discovered{};
    const auto discovery=DiscoverTargets(spotify_module,discovered);
    if(discovery!=spotify::DiscoveryResult::Found) {
        char message[220];snprintf(message,sizeof(message),
            "Spotify audio target discovery failed: %s; capture disabled",
            DiscoveryFailure(discovery));Log(LogKind::Failure,message);
        audio_init.MarkUnsupported();return;
    }
    target=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::OggPageSeek);
    flac_targets[0]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacInit);
    flac_targets[1]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacRead);
    flac_targets[2]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacFrame);
    flac_targets[3]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacError);
    {
        auto* base=reinterpret_cast<unsigned char*>(spotify_module);
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        spotify::DecodedOutputTarget decoded{};
        const auto decoded_discovery=spotify::DiscoverDecodedOutputTarget(
            base,nt->OptionalHeader.SizeOfImage,decoded);
        spotify::DecoderOwnershipLayout layout{};
        const auto ownership_discovery=spotify::DiscoverDecoderOwnershipLayout(
            base,nt->OptionalHeader.SizeOfImage,layout);
        if(ownership_discovery==spotify::DiscoveryResult::Found) {
            decoder_ownership_layout=layout;decoder_ownership_layout_available=true;
            decoded_sink_target=base+layout.decoded_output;
            if(DebugLoggingEnabled()) {
                char line[260];snprintf(line,sizeof(line),
                    "ownership event=layout decoded_rva=%u ogg_delta=%d flac_delta=%d aac_delta=%d aac_verified=%d",
                    layout.decoded_output,layout.ogg_capture_to_decoder,layout.flac_capture_to_decoder,
                    layout.aac_capture_to_decoder,layout.aac_verified);Log(LogKind::Diagnostic,line);
            }
        } else {
            char line[160];snprintf(line,sizeof(line),
                "ownership event=layout-unavailable reason=%s",
                DiscoveryFailure(ownership_discovery));Log(LogKind::Diagnostic,line);
            if(decoded_discovery==spotify::DiscoveryResult::Found)decoded_sink_target=base+decoded.rva;
        }
        if(decoded_discovery==spotify::DiscoveryResult::Found&&DebugLoggingEnabled()) {
            playback_pump_target=base+decoded.caller_rva;
            playback_pump_decoder_offset=decoded.decoder_offset;
            char line[180];snprintf(line,sizeof(line),
                "identity-bridge event=layout caller_rva=%u decoder_offset=%d",
                decoded.caller_rva,decoded.decoder_offset);Log(LogKind::Diagnostic,line);
        }
        if(!decoded_sink_target) {
            SetCaptureAccelerationAvailable(false);
            if(preferences.capture_speed>1) {
                SetCaptureSpeed(1);preferences=GetSettings();
                Log(LogKind::Warning,"capture acceleration unsupported on this Spotify build; 1x remains active");
            }
        }
    }
    if(preferences.aac) {
        auto* base=reinterpret_cast<unsigned char*>(spotify_module);
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        spotify::AacHookTargets aac;
        if(spotify::DiscoverAacHookTargets(base,nt->OptionalHeader.SizeOfImage,aac)==spotify::DiscoveryResult::Found) {
            aac_targets[0]=base+aac.initialize;aac_targets[1]=base+aac.push;aac_targets[2]=base+aac.destroy;
        } else Log(LogKind::Warning,"optional AAC capture unsupported on this Spotify build; FLAC/Ogg remain available");
    }
    auto release_resources=[] {
        if(event) { CloseHandle(event); event=nullptr; }
        if(save_event) { CloseHandle(save_event); save_event=nullptr; }
        if(queue) { HeapFree(GetProcessHeap(),0,queue); queue=nullptr; }
        head=tail=count=0;
        original=nullptr; original_flac_init=nullptr;
        original_flac_read=nullptr; original_flac_frame=nullptr; original_flac_error=nullptr;
        target=nullptr;
        for(auto& address:flac_targets) address=nullptr;
        for(auto& address:aac_targets)address=nullptr;
        original_aac_initialize=nullptr;original_aac_push=nullptr;original_aac_destroy=nullptr;
        original_decoded_process=nullptr;decoded_sink_target=nullptr;
        original_playback_pump=nullptr;playback_pump_target=nullptr;playback_pump_decoder_offset=0;
        playback_identity_bridge={};playback_identity_sequence.store(0,std::memory_order_relaxed);
        playback_identity_metadata_generation=0;
        decoder_ownership_layout={};decoder_ownership_layout_available=false;
        ResetDecodedActivityQueue();
        ownership_output_logged={};ownership_output_logged_count=ownership_output_logged_next=0;
        decoded_accelerator.Reset();decoded_fallback_requested.store(false,std::memory_order_release);
        decoded_fallback_reason.store(unsigned(DecodedFallbackReason::None),std::memory_order_release);
        SetCaptureAccelerationAvailable(false);
    };
    save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    queue=static_cast<Slot*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Slot)*capacity));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!save_event || !queue || !event) {
        Log(LogKind::Failure,"audio queue resources unavailable; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    workers_running.store(true,std::memory_order_release);
    HANDLE publisher=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);
    if(!publisher) {
        workers_running.store(false,std::memory_order_release);
        Log(LogKind::Failure,"publication worker startup failed; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    SetThreadPriority(publisher,THREAD_PRIORITY_BELOW_NORMAL);
    HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(!worker) {
        workers_running.store(false,std::memory_order_release); SetEvent(save_event);
        const bool stopped=WaitForSingleObject(publisher,5000)==WAIT_OBJECT_0;CloseHandle(publisher);
        if(stopped) {Log(LogKind::Failure,"audio worker startup failed; initialization will retry");release_resources();audio_init.Retry(now);}
        else {Log(LogKind::Failure,"audio worker startup failed and publisher did not quiesce; state preserved");audio_init.MarkUnsupported();}
        return;
    }
    auto close_threads=[&] {CloseHandle(publisher);CloseHandle(worker);publisher=nullptr;worker=nullptr;};
    void* created_targets[10]={};unsigned created_count=0;
    auto rollback=[&] {
        hooks::RollbackStatus result;
        auto disabled=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_DISABLED||value==MH_ERROR_NOT_CREATED;};
        auto removed=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_NOT_CREATED;};
        for(unsigned i=0;i<created_count;i++)result.ObserveDisable(disabled(MH_DisableHook(created_targets[i])));
        for(unsigned waited=0;audio_callbacks.Active()&&waited<5000;++waited)Sleep(1);
        result.ObserveQuiescence(audio_callbacks.Active()==0);
        if(result.quiescent)for(unsigned i=0;i<created_count;i++)result.ObserveRemove(removed(MH_RemoveHook(created_targets[i])));
        if(!result.CanRelease()){close_threads();return false;}
        workers_running.store(false,std::memory_order_release);SetEvent(event);SetEvent(save_event);
        const bool worker_stopped=WaitForSingleObject(worker,5000)==WAIT_OBJECT_0;
        const bool publisher_stopped=WaitForSingleObject(publisher,5000)==WAIT_OBJECT_0;
        close_threads();
        if(!worker_stopped||!publisher_stopped)return false;
        release_resources();return true;
    };
    auto create_hook=[&](void* address,void* callback,void** trampoline) {
        MH_STATUS result=MH_CreateHook(address,callback,trampoline);
        if(result==MH_OK)created_targets[created_count++]=address;
        return result;
    };
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=create_hook(target,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original));
    void* flac_callbacks[]={reinterpret_cast<void*>(FlacInitHook),reinterpret_cast<void*>(FlacReadHook),reinterpret_cast<void*>(FlacFrameHook),reinterpret_cast<void*>(FlacErrorHook)};
    void** flac_originals[]={reinterpret_cast<void**>(&original_flac_init),reinterpret_cast<void**>(&original_flac_read),reinterpret_cast<void**>(&original_flac_frame),reinterpret_cast<void**>(&original_flac_error)};
    for(unsigned i=0;status==MH_OK && i<4;i++) {
        status=create_hook(flac_targets[i],flac_callbacks[i],flac_originals[i]);
        if(status==MH_OK) status=MH_QueueEnableHook(flac_targets[i]);
    }
    if(status==MH_OK&&aac_targets[0]) {
        void* callbacks[]={reinterpret_cast<void*>(AacInitializeHook),reinterpret_cast<void*>(AacPushHook),reinterpret_cast<void*>(AacDestroyHook)};
        void** originals[]={reinterpret_cast<void**>(&original_aac_initialize),reinterpret_cast<void**>(&original_aac_push),reinterpret_cast<void**>(&original_aac_destroy)};
        const auto first_aac=created_count;auto aac_status=MH_OK;
        for(unsigned i=0;aac_status==MH_OK&&i<3;++i) {
            aac_status=create_hook(aac_targets[i],callbacks[i],originals[i]);
            if(aac_status==MH_OK)aac_status=MH_QueueEnableHook(aac_targets[i]);
        }
        if(aac_status!=MH_OK) {
            // None of these detours has been activated. Remove just the
            // optional group and retain the supported FLAC/Ogg capability.
            bool removed_all=true;
            for(unsigned i=first_aac;i<created_count;++i) {
                const auto removed=MH_RemoveHook(created_targets[i]);
                if(removed!=MH_OK){status=removed;removed_all=false;}
            }
            // Keep failed removals in the shared rollback registry.
            if(removed_all) {
                created_count=first_aac;
                for(auto& address:aac_targets)address=nullptr;
            }
            Log(LogKind::Warning,"optional AAC hook preparation failed; AAC capture unavailable");
        }
    }
    if(status==MH_OK&&decoded_sink_target) {
        const auto first_optional=created_count;
        const auto optional=hooks::PrepareOptional(
            [&]{return create_hook(decoded_sink_target,reinterpret_cast<void*>(DecodedSinkHook),reinterpret_cast<void**>(&original_decoded_process))==MH_OK;},
            [&]{return MH_QueueEnableHook(decoded_sink_target)==MH_OK;},
            [&]{const auto removed=MH_RemoveHook(decoded_sink_target);if(removed!=MH_OK)status=removed;return removed==MH_OK;});
        if(optional!=hooks::OptionalPreparation::Ready) {
            if(optional==hooks::OptionalPreparation::Unavailable) {
                created_count=first_optional;decoded_sink_target=nullptr;original_decoded_process=nullptr;
                SetCaptureAccelerationAvailable(false);SetCaptureSpeed(1);
            }
            Log(LogKind::Warning,"optional acceleration hook preparation failed; 1x capture remains available");
        }
    }
    if(status==MH_OK&&playback_pump_target) {
        const auto first_optional=created_count;
        const auto optional=hooks::PrepareOptional(
            [&]{return create_hook(playback_pump_target,reinterpret_cast<void*>(PlaybackPumpHook),reinterpret_cast<void**>(&original_playback_pump))==MH_OK;},
            [&]{return MH_QueueEnableHook(playback_pump_target)==MH_OK;},
            [&]{const auto removed=MH_RemoveHook(playback_pump_target);if(removed!=MH_OK)status=removed;return removed==MH_OK;});
        if(optional!=hooks::OptionalPreparation::Ready) {
            if(optional==hooks::OptionalPreparation::Unavailable) {
                created_count=first_optional;playback_pump_target=nullptr;original_playback_pump=nullptr;
                playback_pump_decoder_offset=0;
            }
            Log(LogKind::Diagnostic,"identity-bridge event=hook-unavailable; ordinary capture unchanged");
        }
    }
    if(status==MH_OK) status=MH_QueueEnableHook(target);
    bool apply_attempted=false;
    if(status==MH_OK) {apply_attempted=true;status=MH_ApplyQueued();}
    if(status!=MH_OK) {
        char message[200];snprintf(message,sizeof(message),"audio hook transaction status=%s apply_attempted=%d",MH_StatusToString(status),apply_attempted);Log(LogKind::Diagnostic,message);
        if(apply_attempted) {
            // MH_ApplyQueued may have published some detours before reporting a
            // failure. A thread can be between the detour entry and our C++
            // callback counter, so keep every trampoline and callback resource
            // alive for the process lifetime after disabling the hooks.
            for(unsigned i=0;i<created_count;i++) MH_DisableHook(created_targets[i]);
            workers_running.store(false,std::memory_order_release);SetEvent(event);SetEvent(save_event);
            WaitForSingleObject(worker,5000);WaitForSingleObject(publisher,5000);close_threads();
            Log(LogKind::Failure,"audio hook activation failed; disabled hook state preserved for callback safety");
            audio_init.MarkUnsupported();
        } else if(rollback()){Log(LogKind::Failure,"audio hook preparation failed; rollback complete; retry scheduled");audio_init.Retry(now);}
        else {Log(LogKind::Failure,"audio hook rollback incomplete; callback state preserved and retries disabled");audio_init.MarkUnsupported();}
        return;
    }
    close_threads();
    SetCaptureAccelerationAvailable(decoded_sink_target!=nullptr);
    if(decoded_sink_target) {
        char message[96];snprintf(message,sizeof(message),"capture acceleration available; selected speed=%ux",CaptureSpeed());
        Log(LogKind::Activity,message);
    }
    audio_init.Activate();
}
