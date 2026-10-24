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
#include "async_log.h"
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
#include "image_info.h"
#include "aac_stream.h"
#include "spotify_hook_discovery.h"
#include "vendor/minhook/include/MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
using namespace history;
struct OggPage { unsigned char* header; int32_t header_length; unsigned char* body; int32_t body_length; };
static_assert(sizeof(OggPage)==32 && offsetof(OggPage,body)==16,"Windows x64 libogg ABI");
using PageSeek = int32_t(*)(void*,OggPage*);
PageSeek original;
void* target;
using Sniff = void*(*)(void*,const void*);
Sniff original_sniff;
void* sniff_target;
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
hooks::CallbackCounter audio_callbacks;
enum class AudioEventKind : unsigned { OggPage,FormatDetected,FlacBegin,FlacBytes,FlacFrame,FlacError,FlacEnd,
    AacBegin,AacBytes,AacError,AacDestroy };
enum class FlacErrorSource : unsigned { None,ReadCallback,FrameCallback,DecoderCallback };
struct Slot { uintptr_t context; unsigned length; double time; AudioEventKind kind;unsigned epoch; uint64_t input_length;
    uint32_t format; bool matched,capture;
    FlacErrorSource error_source;
    uint64_t requested,returned; unsigned char bytes[65307]; };
constexpr unsigned capacity=128;
Slot* queue;
unsigned head=0,tail=0,count=0;
SRWLOCK queue_lock=SRWLOCK_INIT;
HANDLE event;
volatile LONG dropped=0,calls=0,pages=0;
std::atomic<bool> quality_enabled{false};
std::wstring output;
size_t memory_limit=500*1024*1024;
static double Now() { return double(GetTickCount64())/1000.0; }
static void Log(const char* message) {
    HistoryLog(message);
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
    explicit NativeTimer(TimingSample& target):sample(target),began(DebugLoggingEnabled()?DiagnosticTick():0) {}
    void Finish() noexcept {
        if(began) {const auto end=DiagnosticTick();if(end>=began)sample.Add(end-began);began=0;}
    }
};
struct QueueGuard {
    uint64_t began,acquired;
    bool producer;
    explicit QueueGuard(bool writing=true):began(DebugLoggingEnabled()?DiagnosticTick():0),acquired(0),producer(writing) {
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
static void LogTiming() {
    if(!DebugLoggingEnabled())return;
    LARGE_INTEGER frequency{};
    if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return;
    const double microseconds=1000000.0/double(frequency.QuadPart);
    struct Named {const char* name;TimingSample* sample;};
    for(const auto& metric:{Named{"native-read",&native_read_timing},{"native-frame",&native_frame_timing},
        {"native-error",&native_error_timing},{"native-init",&native_init_timing},{"native-ogg",&native_ogg_timing},
        {"queue-producer-wait",&producer_wait_timing},{"queue-producer-hold",&producer_hold_timing},
        {"queue-consumer-hold",&consumer_hold_timing}}) {
        const auto n=metric.sample->count.exchange(0,std::memory_order_relaxed);
        const auto maximum=metric.sample->maximum.exchange(0,std::memory_order_relaxed);
        if(n||maximum) {
            char line[180];snprintf(line,sizeof(line),"hook timing metric=%s count=%llu max_us=%.1f callbacks_now=%u",
                metric.name,static_cast<unsigned long long>(n),double(maximum)*microseconds,audio_callbacks.Active());Log(line);
        }
    }
    const auto empty=empty_flac_reads.exchange(0,std::memory_order_relaxed);
    if(empty) {char line[100];snprintf(line,sizeof(line),"native-read continue-with-zero-bytes count=%llu",static_cast<unsigned long long>(empty));Log(line);}
}
static void LogConnectivity() {
    if(!DebugLoggingEnabled())return;
    const auto value=LastConnectivity();
    if(!value.known){Log("network observation=NLM-unknown; stream reachability unknown");return;}
    const auto now=GetTickCount64();
    char line[240];snprintf(line,sizeof(line),
        "network observation=last-NLM age_ms=%llu hr=0x%08lX flags_known=%d original=0x%lX effective=0x%lX; stream reachability unknown",
        static_cast<unsigned long long>(now>=value.observed_ms?now-value.observed_ms:0),
        static_cast<unsigned long>(uint32_t(value.result)),value.flags_valid,
        static_cast<unsigned long>(value.original),static_cast<unsigned long>(value.effective));Log(line);
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
                static_cast<unsigned long long>(sequence),changed);Log(line);
            LogConnectivity();LogTiming();
        }
        known=true;playing=media.playing;status=media.playback_status;
        key=next_key;position=media.position;observed=at;
    }
};
static bool StatusDue(double now,double deadline,const Media& current,bool unavailable) {
    return DebugLoggingEnabled()&&now>=deadline&&current.playing&&!unavailable;
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
    InterlockedIncrement(&calls);
    const bool capture=OggEnabled();
    if(!capture && !quality_enabled.load(std::memory_order_relaxed)) return result;
    if(result<=0 || !page || page->header_length<27 || page->header_length>282 ||
       page->body_length<0 || page->body_length>65025 ||
       page->header_length+page->body_length!=result || result>65307) return result;
    InterlockedIncrement(&pages);
    {
        QueueGuard guard;
        if(count==capacity) InterlockedIncrement(&dropped);
        else {
            Slot& s=queue[tail]; s.context=reinterpret_cast<uintptr_t>(sync);
            s.length=unsigned(result); s.time=Now(); s.kind=AudioEventKind::OggPage; s.epoch=CaptureEpoch(); s.capture=capture;
            std::memcpy(s.bytes,page->header,page->header_length);
            if(page->body_length) std::memcpy(s.bytes+page->header_length,page->body,page->body_length);
            tail=(tail+1)%capacity; ++count;
        }
    }
    SetEvent(event); return result;
}
static void* FormatHook(void* out,const void* input) {
    auto callback=audio_callbacks.Enter();
    // Verified ABI: input is {const byte*, size_t}; result is {uint32_t kind, bool valid}.
    // Read only a short diagnostic prefix. The original detector chooses the format.
    void* result=original_sniff(out,input);
    const auto* view=static_cast<const uintptr_t*>(input);
    unsigned n=unsigned(std::min<uintptr_t>(view[1],16));
    {
        QueueGuard guard;
        if(count==capacity) InterlockedIncrement(&dropped);
        else {
            Slot& s=queue[tail]; s.kind=AudioEventKind::FormatDetected; s.length=n; s.input_length=view[1]; s.time=Now();
            std::memcpy(&s.format,out,4); s.matched=static_cast<unsigned char*>(out)[4]!=0;
            if(n) std::memcpy(s.bytes,reinterpret_cast<const void*>(view[0]),n);
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
    if(status==0 && !*length && DebugLoggingEnabled())empty_flac_reads.fetch_add(1,std::memory_order_relaxed);
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
static void QueueAac(void* context,AudioEventKind kind,const unsigned char* bytes,size_t size,
                     double time,unsigned epoch,unsigned status=0) {
    {
        QueueGuard guard;
        if(count==capacity)InterlockedIncrement(&dropped);
        else {
            auto& s=queue[tail];s.context=reinterpret_cast<uintptr_t>(context);s.kind=kind;
            s.capture=true;s.epoch=epoch;s.time=time;s.length=unsigned(size);s.format=status;
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
    bool lossless=false,metadata=false,aac=false;
    SpacStream spac;
    FlacInfo flac;
    FlacCoverage coverage;
    double born=0,finished=0,retained_until=0;
    uint64_t bytes=0,listen_generation=0;
    CompressedBuffer data;
    size_t Capacity() const {return data.capacity()+(aac?spac.MemoryUsage():0);}
    double Duration() const {
        if(aac)return spac.Duration();
        return lossless&&flac.rate?double(coverage.samples)/flac.rate:stream.Duration();
    }
    Quality Encoding() const {
        if(aac)return {Codec::Aac,spac.info.rate,spac.info.channels,0,0};
        if(lossless)return {Codec::Flac,flac.rate,flac.channels,flac.bits,0};
        return {Codec::Vorbis,stream.rate,stream.channels,0,stream.bitrate};
    }
    uint64_t Samples() const {
        if(aac)return spac.info.total_samples;
        return lossless?coverage.samples:uint64_t(stream.samples);
    }
    uint64_t SourceUnits() const {
        if(aac) {
            const auto samples=uint64_t(spac.info.total_samples)+spac.info.priming;
            return spac.info.samples_per_packet?(samples+spac.info.samples_per_packet-1)/spac.info.samples_per_packet:0;
        }
        return lossless?coverage.frames:stream.pages;
    }
    const wchar_t* Extension() const {return aac?L".m4a":lossless?L".flac":L".ogg";}
    const char* Source() const {
        if(aac)return "native compressed SpAC AAC packets";
        return lossless?"native compressed FLAC input":"native compressed Ogg pages";
    }
};
static bool FormatEnabled(const Capture& capture,const Settings& settings) {
    return capture.aac?settings.aac:capture.lossless?settings.flac:settings.ogg;
}
struct Heard { Media media; double start=0,finish=0;std::uint64_t generation=0; };
constexpr double completion_wait=10;
static bool CaptureTimingMatchesListen(const Capture& capture,const Heard& heard) {
    return (!capture.listen_generation || capture.listen_generation==heard.generation) &&
           capture.born>=heard.start-20 && capture.born<=heard.start+3 && capture.born<heard.finish;
}
static double PendingCaptureDeadline(const Capture& capture,const std::vector<Heard>& heard) {
    for(const auto& pending:heard)
        if(CaptureTimingMatchesListen(capture,pending))return pending.finish+completion_wait;
    return 0;
}
static Catalog CatalogFor(const Media& media) {
    Catalog c; c.title=media.title; c.album=media.album; c.track=media.track;c.duration=media.duration;
    c.artist=media.album_artist.empty() ? media.artist : media.album_artist;
    c.folder_artist=media.album_artist;
    if(_wcsicmp(c.artist.c_str(),L"Various Artists")==0) c.kind=MediaKind::VariousArtists;
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
    std::string text=quality.codec==Codec::Flac ? "FLAC" : quality.codec==Codec::Aac?"AAC (M4A)":"Ogg Vorbis";
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
        LogActivity("renamed","removed filename number prefix; existing audio and metadata retained | "+Utf8(catalog.title));
    }
    return result;
}
enum class Publication { Saved,Skipped,Failed };
static Publication Publish(Capture& c,const Heard& heard,unsigned expected_epoch) {
    const std::string identity=Utf8(heard.media.artist)+" - "+Utf8(heard.media.title);
    auto finish=[&](Publication status,const char* event,const std::string& detail) {
        // Put outcome facts first so a long title cannot truncate quality/size.
        LogActivity(event,detail+" | "+identity);return status;
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
    // Search preferred formats first. Blocked is I/O uncertainty, never proof
    // that a recording is absent. The resolver returns its verified snapshot
    // so skip/metadata planning does not read the entire file again.
    RecordingDestination resolved;
    bool existing_lossless=false,existing_vorbis=false;
    for(const auto codec:{Codec::Flac,Codec::Vorbis}) {
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
    if(decision==SaveDecision::Skip) {
        // An audio duplicate may still have missing metadata. Update only a
        // verified matching recording; this never adds a Latest Downloads entry.
        const auto latest=GetSettings();
        const bool target_enabled=existing_lossless ? settings.flac : existing_vorbis?settings.ogg:FormatEnabled(c,settings);
        if(settings.metadata && settings.update_existing_metadata && target_enabled && latest.generation==settings.generation &&
           latest.downloads && latest.metadata && latest.update_existing_metadata) {
            const auto& path=existing_path;
            if(inspected.owned && inspected.complete && NeedsRecordingMetadata(inspected,tags)) {
                const auto result=UpdateRecordingMetadata(path,tags,catalog,settings.generation);
                if(result==FileMetadataUpdate::Updated) {
                    RefreshSongDestination();Log(("METADATA UPDATED "+Utf8(path)).c_str());
                    return finish(Publication::Saved,"updated","metadata and artwork updated; existing audio retained");
                }
                if(result==FileMetadataUpdate::Failed)
                    return finish(Publication::Failed,"failed","metadata update failed; existing file retained");
            }
        }
        Log(("SKIP existing equal, higher or unverified quality "+Utf8(existing_path)).c_str());
        return finish(Publication::Skipped,"skipped",ExistingSkipReason(existing_path,quality,catalog,&inspected));
    }
    // Existing tags/art/lyrics are merged by PublishBytes while its exact old
    // object is locked. Do not take a separate stale preservation snapshot here.
    const bool had_artwork=inspected.has_artwork;
    resolved.recording={}; // No planning metadata retained across construction.
    std::vector<uint8_t> tagged; std::string error;
    if(c.aac) {
        const auto& info=c.spac.info;
        const auto count=info.samples_per_packet?
            (uint64_t(info.total_samples)+info.priming+info.samples_per_packet-1)/info.samples_per_packet:0;
        if(!AacExtractionFitsBudget(c.data.capacity(),c.data.size(),size_t(count)))
            return finish(Publication::Failed,"failed","AAC extraction memory budget exceeded; no file published");
    }
    auto source=c.data.Flatten();
    bool tagged_ok=false;
    if(c.aac) {
        AacInfo info;std::vector<std::vector<uint8_t>> packets;
        tagged_ok=ReadSpac(source,info,packets,error);
        // Extraction has its own peak bound. Release flattened source bytes
        // before the muxer reserves output; compressed capture blocks were
        // already released by Flatten(). Only one publication runs at a time.
        std::vector<uint8_t>().swap(source);
        if(tagged_ok)tagged_ok=MakeAacM4a(info,packets,tags,tagged,error);
    } else tagged_ok=c.lossless ? TagFlac(source,tags,tagged,error) : TagOgg(source,tags,tagged,error);
    if(!tagged_ok)return finish(Publication::Failed,"failed","tagging failed: "+error);
    auto latest=GetSettings();
    if(latest.generation!=settings.generation || !latest.downloads || !FormatEnabled(c,latest)) return finish(Publication::Skipped,"skipped","capture settings changed or downloads/format disabled");
    if(!Directories(destination.substr(0,destination.find_last_of(L'\\')))) return finish(Publication::Failed,"failed","could not create save directory");
    // Only complete tagged audio reaches the bounded save worker's staged,
    // flushed publication protocol. Recording identity authorizes replacement.
    size_t published_bytes=0;
    auto publication=PublishBytes(destination,quality,tagged.data(),tagged.size(),catalog,&published_bytes);
    if(publication==FilePublication::Skipped)return finish(Publication::Skipped,"skipped",ExistingSkipReason(destination,quality,catalog));
    if(publication==FilePublication::Failed)return finish(Publication::Failed,"failed","file publication failed; existing file retained");
    RefreshSongDestination();
    ImageInfo artwork;
    if(!ReadImageInfo(tags.cover,artwork) && !(publication==FilePublication::Upgraded && had_artwork))
        LogActivity("metadata","artwork unavailable; audio and text tags saved | "+identity);
    RecordRecentDownload(heard.media.artist+L" - "+heard.media.title,destination,published_bytes);
    std::string line=(publication==FilePublication::Upgraded ? "UPGRADED " : "SAVED ")+Utf8(destination); Log(line.c_str());
    return finish(Publication::Saved,publication==FilePublication::Upgraded ? "upgraded" : "finished",SavedAudioDetails(quality,published_bytes));
}
struct SaveJob {std::unique_ptr<Capture> capture;Heard heard;unsigned epoch=0;size_t reserved=0;};
static BoundedQueue<SaveJob,2> saves;
static HANDLE save_event=nullptr;
static std::atomic<size_t> publishing_reserved{0};
static std::atomic<unsigned> saved_count{0};
static std::atomic<bool> workers_running{false};
static hooks::InitController audio_init;
// Conversion checks share the existing publisher, but never consume an audio
// queue slot. Rapid track changes retain only one superseding request. Serial
// cancellation rejects an older request after a new track/settings observation.
struct FilenameCheck {Catalog catalog;Settings settings;uint64_t serial=0;};
static SRWLOCK filename_check_lock=SRWLOCK_INIT;
static std::optional<FilenameCheck> pending_filename_check;
// Producer-only state: the capture worker owns these two fields.
static std::string last_filename_check_key;
static uint64_t last_filename_check_serial=0,queued_filename_check_serial=0;
static void CancelFilenameChecks() {
    filename_check_serial.fetch_add(1,std::memory_order_acq_rel);
    last_filename_check_key.clear();last_filename_check_serial=queued_filename_check_serial=0;
}
static void QueueFilenameCheck(const Media& media,const Settings& settings) {
    try {
        if(!settings.downloads || !settings.convert_numbered_filenames || settings.track_number_in_filename || media.title.empty() || !media.playing) {
            if(!last_filename_check_key.empty())CancelFilenameChecks();
            return;
        }
        auto catalog=CatalogFor(media);
        std::string key;
        auto text=[&](const std::string& value){key+=std::to_string(value.size())+":"+value;};
        auto wide=[&](const std::wstring& value){text(Utf8(value));};
        // Use raw identity, not sanitized filenames: A/B and A:B may share a
        // basename but are still distinct recordings when Spotify URI is absent.
        const std::wstring* fields[]={&settings.root,&catalog.title,&catalog.artist,&catalog.folder_artist,&catalog.legacy_artist,&catalog.album,&catalog.author,&catalog.series,&catalog.book,&catalog.show};
        for(const auto* field:fields)wide(*field);
        text(catalog.spotify_uri);
        key+=std::to_string(unsigned(catalog.kind))+":"+std::to_string(catalog.track)+":"+
            std::to_string(catalog.disc)+":"+std::to_string(catalog.episode_year)+":"+
            std::to_string(catalog.duration)+":"+std::to_string(settings.generation)+":"+std::to_string(settings.music_folder);
        // Invalidate the previous intent even if submission loses the try-lock.
        // Retrying this same intent preserves its serial instead of starving I/O.
        if(key!=last_filename_check_key || last_filename_check_serial!=filename_check_serial.load(std::memory_order_acquire)) {
            last_filename_check_serial=filename_check_serial.fetch_add(1,std::memory_order_acq_rel)+1;
            last_filename_check_key.swap(key);
        }
        if(queued_filename_check_serial==last_filename_check_serial)return;
        FilenameCheck check{std::move(catalog),settings,last_filename_check_serial};
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
        return check.serial==filename_check_serial.load(std::memory_order_acquire) &&
            settings.generation==check.settings.generation && settings.downloads &&
            settings.convert_numbered_filenames && !settings.track_number_in_filename;
    };
    if(!current())return;
    RecordingDirectory directory;
    if(!ListRecordingDirectory(OutputPath(check.settings.root,check.catalog,L".flac",check.settings.music_folder,false),directory,&check.catalog))return;
    for(const auto* extension:{L".flac",L".ogg",L".m4a"}) {
        if(!current())return;
        auto resolved=ResolveRecordingDestination(OutputPath(check.settings.root,check.catalog,extension,check.settings.music_folder,false),check.catalog,false,&directory);
        if(!current())return;
        if(resolved.state==RecordingDestinationState::Blocked)return;
        if(resolved.state!=RecordingDestinationState::Existing || !resolved.recording.complete)continue;
        if(ConvertResolvedFilename(resolved,check.catalog,check.settings,extension,check.serial)==FilenameConversion::Failed)
            LogActivity("failed","filename conversion deferred; existing recording retained | "+Utf8(check.catalog.title));
        return; // Best verified format/recording found; no new audio is created.
    }
}
static DWORD WINAPI SaveWorker(LPVOID) {
    while(workers_running.load(std::memory_order_acquire)) {
        SaveJob job;
        if(!saves.Pop(job)) {
            auto check=TakeFilenameCheck();
            if(check) {
                try {ProcessFilenameCheck(*check);}catch(...) {Log("filename check failed; existing recordings retained");}
                continue;
            }
            WaitForSingleObject(save_event,200);continue;
        }
        std::string identity=Utf8(job.heard.media.artist)+" - "+Utf8(job.heard.media.title);
        try {
            auto result=Publish(*job.capture,job.heard,job.epoch);
            if(result==Publication::Saved) {
                auto settings=GetSettings();
                if(settings.capture_epoch==job.epoch) ++saved_count;
            }
        } catch(...) {LogActivity("failed",identity+" (publication exception)");}
        job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
    }
    return 0;
}
struct CaptureBuffers {
    std::map<uintptr_t,std::unique_ptr<Capture>> active;
    std::vector<std::unique_ptr<Capture>> ready;
};
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
static void LogCaptureContext(const Slot& s,const Capture& capture,const char* origin) {
    if(!DebugLoggingEnabled())return;
    char line[430];snprintf(line,sizeof(line),
        "FLAC observation origin=%s ctx=%llx mono=%.3f age=%.3f queue_lag_ms=%.1f epoch=%u metadata=%d frames=%u covered=%llu total=%llu bytes=%llu dropped=%ld",
        origin,static_cast<unsigned long long>(s.context),s.time,s.time-capture.born,std::max(0.0,Now()-s.time)*1000,s.epoch,capture.metadata,
        capture.coverage.frames,static_cast<unsigned long long>(capture.coverage.samples),
        static_cast<unsigned long long>(capture.flac.total_samples),static_cast<unsigned long long>(capture.bytes),dropped);Log(line);
}
static void ProcessFlacCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    auto found=active.find(s.context);
    if(s.kind==AudioEventKind::FlacError && DebugLoggingEnabled()) {
        char line[240];snprintf(line,sizeof(line),
            "FLAC native result ctx=%llx origin=%s code=%u requested=%llu returned=%llu mono=%.3f epoch=%u",
            static_cast<unsigned long long>(s.context),ErrorOrigin(s.error_source),s.format,
            static_cast<unsigned long long>(s.requested),static_cast<unsigned long long>(s.returned),s.time,s.epoch);Log(line);
        LogConnectivity();LogTiming();
    }
    if(s.kind==AudioEventKind::FlacBegin) {
        if(found!=active.end())LogCaptureContext(s,*found->second,"context-reinitialized");
        auto c=std::make_unique<Capture>(); c->lossless=true; c->born=s.time;
        LogCaptureContext(s,*c,"begin");
        active[s.context]=std::move(c); Log("FLAC decoder initialized; capture begins at compressed offset zero"); return;
    }
    if(found==active.end() || !found->second->lossless) return;
    Capture& c=*found->second;
    if(s.kind==AudioEventKind::FlacError) {
        LogCaptureContext(s,c,ErrorOrigin(s.error_source));
        active.erase(found); Log("FLAC decoder error; stream discarded"); return;
    }
    if(s.kind==AudioEventKind::FlacBytes) {
        size_t reserved=publishing_reserved.load(); for(const auto& item:active) reserved+=item.second->Capacity();
        for(const auto& item:ready) reserved+=item->Capacity();
        size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
        if(!c.data.Append(s.bytes,s.length,available)) { active.erase(found); Log("FLAC capture memory limit; stream discarded"); return; }
        c.bytes+=s.length;
        if(!c.metadata) {
            size_t audio=0; auto parsed=ParseFlacMetadata(c.data,c.flac,audio);
            if(parsed==FlacParse::Invalid) { active.erase(found); Log("FLAC metadata invalid; stream discarded"); return; }
            if(parsed==FlacParse::Valid) {
                c.metadata=true; char line[180]; snprintf(line,sizeof(line),"FLAC STREAMINFO rate=%u channels=%u bits=%u total=%llu metadata_bytes=%zu",c.flac.rate,c.flac.channels,c.flac.bits,static_cast<unsigned long long>(c.flac.total_samples),audio); Log(line);
            }
        }
        return;
    }
    if(s.kind==AudioEventKind::FlacFrame) {
        uint32_t frame[6]; uint64_t number; memcpy(frame,s.bytes,24); memcpy(&number,s.bytes+24,8);
        if(frame[5]==0) number=uint32_t(number);
        const char* reason="metadata-not-ready";
        if(!c.metadata || !c.coverage.Frame(c.flac,frame[0],frame[1],frame[2],frame[4],frame[5],number,&reason)) {
            LogCaptureContext(s,c,"capture-coverage-guard");
            if(DebugLoggingEnabled()) {
                char line[430];snprintf(line,sizeof(line),
                    "FLAC coverage ctx=%llx reason=%s native_frame_result=0 expected_rate=%u got_rate=%u expected_channels=%u got_channels=%u expected_bits=%u got_bits=%u block=%u min_block=%u max_block=%u number_type=%u number=%llu expected_sample=%llu",
                    static_cast<unsigned long long>(s.context),reason,c.flac.rate,frame[1],c.flac.channels,frame[2],c.flac.bits,frame[4],
                    frame[0],c.flac.min_block,c.flac.max_block,frame[5],static_cast<unsigned long long>(number),
                    static_cast<unsigned long long>(c.coverage.samples));Log(line);
                LogConnectivity();LogTiming();
            }
            active.erase(found); Log("FLAC decoded-frame gap or format mismatch; stream discarded"); return;
        }
        if(!c.coverage.Complete(c.flac)) return;
    } else if(s.kind!=AudioEventKind::FlacEnd || !c.coverage.Complete(c.flac)) {
        LogCaptureContext(s,c,"end-before-coverage");LogConnectivity();LogTiming();
        active.erase(found); Log("FLAC truncated before total samples; discarded"); return;
    }
    LogCaptureContext(s,c,"complete");
    c.finished=s.time;
    char line[180]; snprintf(line,sizeof(line),"FLAC COMPLETE frames=%u samples=%llu bytes=%llu duration=%.6f",c.coverage.frames,static_cast<unsigned long long>(c.coverage.samples),static_cast<unsigned long long>(c.bytes),c.Duration()); Log(line);
    ready.push_back(std::move(found->second)); active.erase(found); return;
}
static void ProcessOggCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    Page page; if(!ParsePage(s.bytes,s.length,page)) {
        active.erase(s.context); Log("invalid Ogg page rejected"); return;
    }
    if(page.vorbis_start) {
        auto c=std::make_unique<Capture>(); c->born=s.time;
        active[s.context]=std::move(c);
        char line[160]; snprintf(line,sizeof(line),"BOS ctx=%llx seq=%u serial=%u rate=%u channels=%u",
            static_cast<unsigned long long>(s.context),page.sequence,page.serial,page.rate,page.channels); Log(line);
    }
    auto found=active.find(s.context); if(found==active.end()) return;
    Capture& c=*found->second; Result result=c.stream.Push(s.bytes,s.length);
    if(result==Result::Invalid || result==Result::Ignore) {
        active.erase(found); Log("stream discarded: page gap or integrity failure"); return;
    }
    size_t reserved=publishing_reserved.load();
    for(const auto& item:active) reserved+=item.second->Capacity();
    for(const auto& item:ready) reserved+=item->Capacity();
    size_t available=reserved<memory_limit ? memory_limit-reserved : 0;
    if(!c.data.Append(s.bytes,s.length,available)) {
        active.erase(found);
        Log("capture memory limit reached; track discarded without scratch files"); return;
    }
    c.bytes+=s.length;
    if(result==Result::Complete) {
        c.finished=s.time;
        char line[200]; snprintf(line,sizeof(line),"EOS pages=%u bytes=%llu duration=%.6f capture_seconds=%.3f",
          c.stream.pages,static_cast<unsigned long long>(c.bytes),c.stream.Duration(),c.finished-c.born); Log(line);
        ready.push_back(std::move(found->second)); active.erase(found);
    }
}
static void ProcessAacCapture(const Slot& s,CaptureBuffers& buffers) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    auto found=active.find(s.context);
    if(s.kind==AudioEventKind::AacBegin) {
        if(found==active.end()&&active.size()>=16){Log("AAC decoder limit reached; stream ignored");return;}
        auto capture=std::make_unique<Capture>();capture->aac=true;capture->born=s.time;
        active[s.context]=std::move(capture);return;
    }
    if(found==active.end()||!found->second->aac)return;
    if(s.kind==AudioEventKind::AacDestroy||s.kind==AudioEventKind::AacError) {
        active.erase(found);
        if(s.kind==AudioEventKind::AacError)Log("AAC decoder input rejected; stream discarded");
        return;
    }
    auto& c=*found->second;
    auto reserved=[&] {
        size_t total=publishing_reserved.load();
        for(const auto& entry:active)total+=entry.second->Capacity();
        for(const auto& entry:ready)total+=entry->Capacity();
        return total;
    };
    const auto used=reserved();
    if(!c.spac.MemoryUsage()&&(used>=memory_limit||memory_limit-used<65536+64)) {
        active.erase(found);Log("AAC capture memory limit; stream discarded");return;
    }
    const auto result=c.spac.Push(s.bytes,s.length);
    if(result==AacParse::Invalid||result==AacParse::Unsupported) {
        active.erase(found);Log(result==AacParse::Unsupported?
            "unsupported SpAC AAC configuration; stream discarded":"AAC page checksum or frame gap; stream discarded");return;
    }
    const auto allocated=reserved();
    if(!c.data.Append(s.bytes,s.length,allocated<memory_limit?memory_limit-allocated:0)) {
        active.erase(found);Log("AAC capture memory limit; stream discarded");return;
    }
    c.bytes+=s.length;
    if(result==AacParse::Complete) {
        c.finished=s.time;
        char line[180];snprintf(line,sizeof(line),"AAC COMPLETE source_bytes=%llu rate=%u channels=%u samples=%u priming=%u duration=%.6f",
            static_cast<unsigned long long>(c.bytes),c.spac.info.rate,c.spac.info.channels,c.spac.info.total_samples,c.spac.info.priming,c.Duration());Log(line);
        ready.push_back(std::move(found->second));active.erase(found);
    }
}
static void ProcessCaptureEvent(const Slot& s,CaptureBuffers& buffers,PlaybackQualityTracker& quality,
                                bool observing_quality,bool enabled,unsigned epoch) {
    if(s.kind==AudioEventKind::FormatDetected) {
        char prefix[33]={};for(unsigned i=0;i<s.length;++i)snprintf(prefix+2*i,3,"%02x",s.bytes[i]);
        char line[180];snprintf(line,sizeof(line),"FORMAT kind=%u matched=%d input_bytes=%llu prefix=%s",
            s.format,s.matched,static_cast<unsigned long long>(s.input_length),prefix);Log(line);return;
    }
    if(observing_quality) ObserveCodecQuality(s,quality);
    if(!enabled || !s.capture || s.epoch!=epoch) return;
    if(s.kind>=AudioEventKind::AacBegin)ProcessAacCapture(s,buffers);
    else if(s.kind>=AudioEventKind::FlacBegin) ProcessFlacCapture(s,buffers);
    else ProcessOggCapture(s,buffers);
}
static void AssociateListen(Media media,Listen& listen,Media& current,std::vector<Heard>& heard,
                            CaptureBuffers& buffers,double time) {
    auto& active=buffers.active;auto& ready=buffers.ready;
    double previous_start=listen.start_time;
    const auto previous_generation=listen.generation;
    bool was_eligible=listen.eligible;
    double prior_position=listen.last_position,prior_duration=listen.duration,prior_time=listen.last_time;
    std::string done=listen.Observe(media.Key(),media.position,media.duration,media.playing,time);
    if(was_eligible && !listen.eligible && done.empty()) {
        char line[300]; snprintf(line,sizeof(line),"listen invalidated old=%.6f/%.6f new=%.6f/%.6f delta=%.3f playing=%d",
            prior_position,prior_duration,media.position,media.duration,time-prior_time,media.playing); Log(line);
    }
    if(!done.empty()) { heard.push_back({current,listen.completed_start,listen.completed_time,listen.completed_generation}); Log(("FULL LISTEN "+Utf8(current.title)).c_str()); }
    if(previous_generation!=listen.generation && listen.eligible) LogActivity("started",Utf8(media.artist)+" - "+Utf8(media.title));
    if(was_eligible && !listen.eligible && done.empty()) LogActivity("failed",Utf8(current.artist)+" - "+Utf8(current.title)+" (incomplete listen)");
    if(previous_generation!=listen.generation) {
        Log(("TRACK "+Utf8(media.artist)+" - "+Utf8(media.title)).c_str());
        {
            auto prior=[previous_start,start=listen.start_time](const auto& c) {
                return c->born>=previous_start-20 && c->born<=previous_start+3 && c->born<start-3;
            };
            // A fast Next may create the new decoder inside the
            // previous track's three-second start window. Keep
            // that candidate; duration association still rejects
            // ambiguous or skipped streams at publication.
            if(done.empty())
                ready.erase(std::remove_if(ready.begin(),ready.end(),[&](const auto& c) {
                    return prior(c) && !PendingCaptureDeadline(*c,heard);
                }),ready.end());
            for(auto it=active.begin();it!=active.end();) {
                if(prior(it->second)) {
                    // SMTC may announce the next track before the final codec
                    // callback. Keep that bounded candidate for the completed
                    // listen; an unfinished stream cannot pass a duration check.
                    const auto deadline=PendingCaptureDeadline(*it->second,heard);
                    if(deadline<=time) {it=active.erase(it);continue;}
                    auto& retained=it->second->retained_until;
                    retained=retained?std::min(retained,deadline):deadline;
                }
                ++it;
            }
        }
    }
    if(listen.transient) Log("timeline reset at natural end; waiting for matching title");
    else current=std::move(media);
}
static bool CaptureMatchesListen(const Capture& capture,const Heard& heard) {
    return CaptureTimingMatchesListen(capture,heard) &&
           (!capture.retained_until || capture.finished<=capture.retained_until) &&
           std::fabs(capture.Duration()-heard.media.duration)<=1.0;
}
static std::string AssociationFailure(const Heard& heard,const CaptureBuffers& buffers,size_t matches) {
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
    char counts[260];
    snprintf(counts,sizeof(counts)," | matches=%zu ready=%zu timing_ready=%zu duration_ready=%zu active=%zu timing_active=%zu (FLAC=%zu Ogg=%zu) expected=%.3fs",
        matches,buffers.ready.size(),timing_ready,duration_ready,buffers.active.size(),timing_active,flac_active,ogg_active,heard.media.duration);
    return std::string("completed listen discarded: ")+
        (matches>1?"ambiguous completed captures":"no matching complete capture")+counts+
        " | "+Utf8(heard.media.artist)+" - "+Utf8(heard.media.title);
}
static void SchedulePublications(std::vector<Heard>& heard,CaptureBuffers& buffers,unsigned epoch,double now) {
    auto& ready=buffers.ready;
    for(auto h=heard.begin();h!=heard.end();) {
        size_t match=ready.size(),matches=0;
        for(size_t i=0;i<ready.size();i++) {
            Capture& c=*ready[i];
            if(CaptureMatchesListen(c,*h)) { match=i; ++matches; }
        }
        if(matches==1) {
            SaveJob job;job.reserved=ready[match]->Capacity();job.epoch=epoch;
            job.capture=std::move(ready[match]);job.heard=*h;job.capture->listen_generation=h->generation;
            publishing_reserved.fetch_add(job.reserved);
            if(saves.Push(std::move(job)))SetEvent(save_event);
            else {size_t reserved=job.reserved;job.capture.reset();publishing_reserved.fetch_sub(reserved);LogActivity("failed","publication queue full; completed candidate discarded");}
            ready.erase(ready.begin()+match); h=heard.erase(h);
        } else if(matches>1 || now-h->finish>completion_wait) {
            LogActivity("failed",AssociationFailure(*h,buffers,matches)); h=heard.erase(h);
        } else ++h;
    }
    // Release transition-only retention once its listen was queued/discarded,
    // or its original completion deadline passed. Do not extend it on repeats.
    auto expired=[&](const auto& capture) {
        return capture->retained_until &&
            (now>capture->retained_until || !PendingCaptureDeadline(*capture,heard));
    };
    for(auto it=buffers.active.begin();it!=buffers.active.end();) {
        if(expired(it->second))it=buffers.active.erase(it);else ++it;
    }
    ready.erase(std::remove_if(ready.begin(),ready.end(),expired),ready.end());
}
static DWORD WINAPI Worker(LPVOID) {
    LONG overflow=0;
    CaptureBuffers buffers;auto& active=buffers.active;auto& ready=buffers.ready;
    std::vector<Heard> heard;
    Listen listen; MediaSnapshotReader reader; Media current; PlaybackQualityTracker quality; std::string client_quality;
    uint64_t media_sequence=0;
    PlaybackDiagnostics diagnostics;
    double last_media=Now(),minimum_media_time=last_media,next_log=0;
    bool observer_started=false,media_unavailable=false;
    unsigned generation=~0u,epoch=~0u; bool enabled=false;
    try {
        while(workers_running.load(std::memory_order_acquire)) {
            double now=Now();
            auto preferences=GetSettings();
            if(preferences.generation!=generation) {
                bool initial=generation==~0u;
                generation=preferences.generation;
                output=preferences.root; memory_limit=size_t(preferences.max_buffered_mib)*1024*1024;
                bool capture=preferences.downloads && (preferences.ogg || preferences.flac || preferences.aac);
                if(capture!=enabled || initial || preferences.capture_epoch!=epoch) {
                    active.clear(); ready.clear(); heard.clear(); listen={}; current={};
                    last_media=minimum_media_time=now;media_unavailable=false;diagnostics.known=false;CancelFilenameChecks();
                    if(!enabled && capture) saved_count=0;
                    enabled=capture;
                    Log(enabled ? "To Disk capture enabled" : "To Disk capture disabled");
                }
                epoch=preferences.capture_epoch;
            }
            if(!enabled && !quality_enabled.load(std::memory_order_relaxed)) {
                reader.Stop();observer_started=false;
                PublishPlaybackQuality({});
                PublishSongDestination(0,L"",{},false,0);
                Slot discard; while(Pop(discard)) {}
                WaitForSingleObject(event,200); continue;
            }
            if(dropped!=overflow) {
                overflow=dropped; active.clear(); ready.clear(); heard.clear(); listen.eligible=false; quality.ClearStreams();
                Log("capture queue overflow; streams and listening coverage invalidated");
            }
            Slot s;
            while(Pop(s)) ProcessCaptureEvent(s,buffers,quality,quality_enabled.load(std::memory_order_relaxed),enabled,epoch);
            // Start only the basic media observer. All COM/WinRT work stays on
            // its thread; this consumer only try-locks a bounded RAM snapshot.
            // A still-stopping observer must finish before another can start.
            if(!observer_started) observer_started=reader.Start();
            Media media;double observed_at=0;
            auto observation=observer_started?reader.TryRead(media,observed_at,media_sequence):MediaRead::Pending;
            if(observation!=MediaRead::Pending && observed_at<minimum_media_time)
                observation=MediaRead::Pending; // Never reuse an earlier capture epoch.
            if(observation==MediaRead::Ready) {
                last_media=observed_at;media_unavailable=false;
                diagnostics.Observe(media,observed_at,media_sequence);
                PublishCurrentDestination(media,preferences);
                quality.Media(media.Key(),media.title,media.position,media.duration,observed_at,media.playing);
                client_quality=ReadClientPlaybackQuality(media);
                if(enabled)AssociateListen(std::move(media),listen,current,heard,buffers,observed_at);
            } else if(observation==MediaRead::Failed || Now()-last_media>3) {
                // A stalled provider cannot stall decoder events, but missing
                // observations still cannot establish continuous listening.
                PublishSongDestination(0,L"",{},false,0);
                listen.eligible=false;quality.Media({},L"",0,0,Now());client_quality.clear();
                if(!media_unavailable){CancelFilenameChecks();Log("Spotify media snapshot unavailable or stale; listen invalidated");}
                media_unavailable=true;diagnostics.known=false;
            }
            if(!enabled) {
                PublishPlaybackQuality(quality.Snapshot(client_quality));
                WaitForSingleObject(event,50);continue;
            }
            now=Now();
            PublishPlaybackQuality(quality.Snapshot(client_quality));
            // Menu settings may change concurrently with snapshot processing.
            // Apply that generation before considering any publication.
            if(GetSettings().generation!=generation) continue;
            SchedulePublications(heard,buffers,epoch,now);
            ready.erase(std::remove_if(ready.begin(),ready.end(),[now](const auto& c){return now-c->finished>600;}),ready.end());
            for(auto it=active.begin();it!=active.end();) {
                if(now-it->second->born>21600) it=active.erase(it); else ++it;
            }
            // A paused/stale timeline adds no useful periodic status. Leave an
            // overdue deadline intact so resumed playback logs immediately.
            if(StatusDue(now,next_log,current,media_unavailable)) {
                size_t buffered=0;
                for(const auto& item:active) buffered+=item.second->Capacity();
                for(const auto& item:ready) buffered+=item->Capacity();
                char line[350]; snprintf(line,sizeof(line),"status calls=%ld pages=%ld dropped=%ld active=%zu ready=%zu saved=%u pos=%.3f/%.3f eligible=%d buffered=%zu",
                  calls,pages,dropped,active.size(),ready.size(),saved_count.load(),current.position,current.duration,listen.eligible,buffered); Log(line); LogTiming(); next_log=now+10;
            }
            WaitForSingleObject(event,50);
        }
    } catch(...) { Log("history worker stopped after an exception; playback remains with original parser"); }
    CancelFilenameChecks();
    reader.Stop(); // Nonblocking even if Windows is still servicing a COM call.
    PublishPlaybackQuality({});
    PublishSongDestination(0,L"",{},false,0);
    workers_running.store(false,std::memory_order_release);
    if(save_event) SetEvent(save_event);
    MH_DisableHook(target);
    MH_DisableHook(sniff_target);
    for(auto address:flac_targets) if(address) MH_DisableHook(address);
    for(auto address:aac_targets) if(address) MH_DisableHook(address);
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
    if(output.empty()) { Log("save location unavailable; history initialization will retry"); audio_init.Retry(now); return; }
    spotify::HookTargets discovered{};
    const auto discovery=DiscoverTargets(spotify_module,discovered);
    if(discovery!=spotify::DiscoveryResult::Found) {
        char message[220];snprintf(message,sizeof(message),
            "Spotify audio target discovery failed: %s; capture disabled",
            DiscoveryFailure(discovery));Log(message);
        audio_init.MarkUnsupported();return;
    }
    target=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::OggPageSeek);
    sniff_target=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FormatSniff);
    flac_targets[0]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacInit);
    flac_targets[1]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacRead);
    flac_targets[2]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacFrame);
    flac_targets[3]=DiscoveredTarget(spotify_module,discovered,spotify::TargetKind::FlacError);
    if(preferences.aac) {
        auto* base=reinterpret_cast<unsigned char*>(spotify_module);
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        spotify::AacHookTargets aac;
        if(spotify::DiscoverAacHookTargets(base,nt->OptionalHeader.SizeOfImage,aac)==spotify::DiscoveryResult::Found) {
            aac_targets[0]=base+aac.initialize;aac_targets[1]=base+aac.push;aac_targets[2]=base+aac.destroy;
        } else Log("optional AAC capture unsupported on this Spotify build; FLAC/Ogg remain available");
    }
    auto release_resources=[] {
        if(event) { CloseHandle(event); event=nullptr; }
        if(save_event) { CloseHandle(save_event); save_event=nullptr; }
        if(queue) { HeapFree(GetProcessHeap(),0,queue); queue=nullptr; }
        head=tail=count=0;
        original=nullptr; original_sniff=nullptr; original_flac_init=nullptr;
        original_flac_read=nullptr; original_flac_frame=nullptr; original_flac_error=nullptr;
        target=nullptr; sniff_target=nullptr;
        for(auto& address:flac_targets) address=nullptr;
        for(auto& address:aac_targets)address=nullptr;
        original_aac_initialize=nullptr;original_aac_push=nullptr;original_aac_destroy=nullptr;
    };
    save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    queue=static_cast<Slot*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Slot)*capacity));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!save_event || !queue || !event) {
        Log("audio queue resources unavailable; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    workers_running.store(true,std::memory_order_release);
    HANDLE publisher=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);
    if(!publisher) {
        workers_running.store(false,std::memory_order_release);
        Log("publication worker startup failed; initialization will retry"); release_resources(); audio_init.Retry(now); return;
    }
    SetThreadPriority(publisher,THREAD_PRIORITY_BELOW_NORMAL);
    HANDLE worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    if(!worker) {
        workers_running.store(false,std::memory_order_release); SetEvent(save_event);
        const bool stopped=WaitForSingleObject(publisher,5000)==WAIT_OBJECT_0;CloseHandle(publisher);
        if(stopped) {Log("audio worker startup failed; initialization will retry");release_resources();audio_init.Retry(now);}
        else {Log("audio worker startup failed and publisher did not quiesce; state preserved");audio_init.MarkUnsupported();}
        return;
    }
    auto close_threads=[&] {CloseHandle(publisher);CloseHandle(worker);publisher=nullptr;worker=nullptr;};
    void* created_targets[9]={};unsigned created_count=0;
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
    if(status==MH_OK) status=create_hook(sniff_target,reinterpret_cast<void*>(FormatHook),reinterpret_cast<void**>(&original_sniff));
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
            Log("optional AAC hook preparation failed; AAC capture unavailable");
        }
    }
    if(status==MH_OK) status=MH_QueueEnableHook(target);
    if(status==MH_OK) status=MH_QueueEnableHook(sniff_target);
    bool apply_attempted=false;
    if(status==MH_OK) {apply_attempted=true;status=MH_ApplyQueued();}
    if(status!=MH_OK) {
        char message[200];snprintf(message,sizeof(message),"audio hook installation failed: %s",MH_StatusToString(status));Log(message);
        if(apply_attempted) {
            // MH_ApplyQueued may have published some detours before reporting a
            // failure. A thread can be between the detour entry and our C++
            // callback counter, so keep every trampoline and callback resource
            // alive for the process lifetime after disabling the hooks.
            for(unsigned i=0;i<created_count;i++) MH_DisableHook(created_targets[i]);
            workers_running.store(false,std::memory_order_release);SetEvent(event);SetEvent(save_event);
            WaitForSingleObject(worker,5000);WaitForSingleObject(publisher,5000);close_threads();
            Log("audio hook activation failed; disabled hook state preserved for callback safety");
            audio_init.MarkUnsupported();
        } else if(rollback()){Log("audio hook rollback complete; retry scheduled");audio_init.Retry(now);}
        else {Log("audio hook rollback incomplete; callback state preserved and retries disabled");audio_init.MarkUnsupported();}
        return;
    }
    close_threads();
    audio_init.Activate();
    Log("native history active; dynamic Spotify targets; memory capture; embedded metadata/art; complete listens only");
}
