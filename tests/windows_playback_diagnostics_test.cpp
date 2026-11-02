// Exercise real hooks/worker diagnostics without patching or controlling Spotify.
#include "../native/async_log.h"
namespace history {void CaptureLogEvent(LogKind,const char*,const std::string&);}
#define LogEvent CaptureLogEvent
#include "../native/audio_history.cpp"
#undef LogEvent
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include <cassert>

namespace {
std::vector<std::string> diagnostic_lines;
unsigned failure_events=0;
bool Contains(const char* text) {
    return std::any_of(diagnostic_lines.begin(),diagnostic_lines.end(),[text](const auto& line){return line.find(text)!=std::string::npos;});
}
unsigned init_calls=0,read_calls=0,frame_calls=0,error_calls=0;
int read_status=0,frame_status=0;
bool oversized_read=false,empty_read=false;
bool invalid_decoded_span=false;
unsigned decode_calls=0;
const void* expected_frame=nullptr;
const void* expected_pcm=nullptr;
void* Init(void* context,void* result) {
    ++init_calls;assert(context==reinterpret_cast<void*>(0x1234));
    assert(count==1&&queue[head].kind==AudioEventKind::FlacBegin);return result;
}
int Read(void* context,unsigned char* buffer,size_t* size) {
    ++read_calls;assert(context==reinterpret_cast<void*>(0x1234));
    if(oversized_read){++*size;return read_status;}
    if(empty_read){*size=0;return 0;}
    if(read_status){*size=0;return read_status;}
    const unsigned char source[]={1,2,3,4};assert(*size>=sizeof(source));
    memcpy(buffer,source,sizeof(source));*size=sizeof(source);return 0;
}
int Frame(void* context,const void* frame,const void* pcm) {
    ++frame_calls;assert(context==reinterpret_cast<void*>(0x1234));
    assert(frame==expected_frame&&pcm==expected_pcm);return frame_status;
}
void Error(void* context,unsigned value) {
    ++error_calls;assert(context==reinterpret_cast<void*>(0x1234));assert(value==2);
}
void* Decode(void*,void* result,decoded_output::Span* decoded,decoded_output::Span* encoded,bool mode) {
    ++decode_calls;assert(mode&&decoded&&encoded);decoded->size-=80;
    decoded->data=static_cast<float*>(decoded->data)+(invalid_decoded_span?79:80);
    encoded->data=static_cast<unsigned char*>(encoded->data)+100;
    encoded->size-=100;SetLastError(1234);return result;
}
void ObservePlayback() {
    PlaybackDiagnostics state;Media media;media.title=L"Song";media.artist=L"Artist";
    media.position=1;media.duration=100;media.playing=true;media.playback_status=4;
    diagnostic_lines.clear();state.Observe(media,10,1);assert(Contains("baseline")&&Contains("initiator=unknown"));
    media.position=1.5;state.Observe(media,10.5,2);const auto before=diagnostic_lines.size();
    state.Observe(media,11,3);assert(diagnostic_lines.size()==before);
    media.playing=false;media.playback_status=5;state.Observe(media,11.5,4);assert(Contains("state=paused"));
    const auto paused=diagnostic_lines.size();
    for(unsigned i=0;i<12;++i)state.Observe(media,12+i,5+i);
    assert(diagnostic_lines.size()==paused);
    media.playback_status=3;state.Observe(media,25,18);assert(Contains("state=stopped"));
    media.playback_status=2;state.Observe(media,25.5,19);assert(Contains("state=changing"));
    media.playing=true;media.playback_status=4;state.Observe(media,26,20);
    media.position=0;state.Observe(media,26.5,21);assert(Contains("timeline-jump"));
    media.title=L"Next";state.Observe(media,27,22);assert(Contains("track_changed=1"));
    InterlockedExchange(&history::debug_enabled,0);const auto off=diagnostic_lines.size();
    state.Observe(media,27.5,23);assert(diagnostic_lines.size()==off);
    InterlockedExchange(&history::debug_enabled,1);
}
void CoverageAndFailures() {
    FlacInfo info{44100,2,16,16,16,32};const char* reason=nullptr;FlacCoverage coverage;
    assert(coverage.Frame(info,16,44100,2,16,0,0,&reason)&&!reason);
    const auto reject=[&](unsigned block,unsigned rate,unsigned channels,unsigned bits,unsigned type,uint64_t number,const char* expected) {
        assert(!coverage.Frame(info,block,rate,channels,bits,type,number,&reason));
        assert(reason&&std::string(reason)==expected&&coverage.samples==16&&coverage.frames==1);
    };
    reject(0,44100,2,16,0,1,"block-size");reject(16,48000,2,16,0,1,"format");
    reject(16,44100,1,16,0,1,"format");reject(16,44100,2,24,0,1,"format");
    reject(16,44100,2,16,2,1,"number-type");reject(16,44100,2,16,0,UINT64_MAX,"frame-number");
    reject(16,44100,2,16,1,0,"sample-position");
    assert(coverage.Frame(info,16,44100,2,16,1,16,&reason)&&coverage.Complete(info));
    assert(!coverage.Frame(info,16,44100,2,16,1,32,&reason)&&std::string(reason)=="sample-count");
    CaptureBuffers buffers;Slot slot{};slot.context=0x1234;slot.kind=AudioEventKind::FlacBegin;slot.time=Now();slot.epoch=7;
    ProcessFlacCapture(slot,buffers);
    const auto bytes=recording_fixture::fixture();slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(bytes.size());
    memcpy(slot.bytes,bytes.data(),bytes.size());ProcessFlacCapture(slot,buffers);assert(buffers.active.at(slot.context)->metadata);
    uint32_t frame[8]={16,48000,2,0,16,0,0,0};slot.kind=AudioEventKind::FlacFrame;slot.length=32;memcpy(slot.bytes,frame,32);
    diagnostic_lines.clear();const auto prior_failures=failure_events;ProcessFlacCapture(slot,buffers);
    assert(buffers.active.empty()&&buffers.ready.empty());
    assert(diagnostic_lines.size()==3&&failure_events==prior_failures+2); // Context, final-cache check, native failure outcome.
    assert(Contains("final cache check:"));
    assert(Contains("origin=capture-coverage-guard")&&Contains("reason=format")&&Contains("native_frame_result=0"));
    assert(Contains("expected_rate=44100 got_rate=48000")&&Contains("ctx=1234")&&Contains("queue_lag_ms="));
    // Native errors remain visible even after our capture has already ended.
    slot.kind=AudioEventKind::FlacError;slot.error_source=FlacErrorSource::ReadCallback;slot.format=2;slot.requested=8192;
    diagnostic_lines.clear();ProcessFlacCapture(slot,buffers);assert(Contains("origin=native-read-return")&&Contains("code=2 requested=8192 returned=0"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacFrame;memcpy(slot.bytes,frame,32);diagnostic_lines.clear();ProcessFlacCapture(slot,buffers);
    assert(Contains("reason=metadata-not-ready"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    InterlockedExchange(&history::debug_enabled,0);diagnostic_lines.clear();
    slot.kind=AudioEventKind::FlacFrame;ProcessFlacCapture(slot,buffers);
    assert(diagnostic_lines.size()==2&&Contains("stream discarded")&&Contains("final cache check:"));
    InterlockedExchange(&history::debug_enabled,1);
}
void CaptureOwnershipLifecycle() {
    decoder_ownership_layout={};decoder_ownership_layout.ogg_capture_to_decoder=-0x88;
    decoder_ownership_layout.flac_capture_to_decoder=0;decoder_ownership_layout_available=true;
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(7);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;

    Slot slot{};slot.context=0x2000;slot.kind=AudioEventKind::FlacBegin;slot.time=1;slot.epoch=7;
    ProcessFlacCapture(slot,buffers);auto* flac=buffers.active.at(slot.context).get();
    assert(flac->decoder_owner==0x2000&&flac->capture_token);
    assert(tracker.Binding(flac->capture_token).state==ownership::OwnershipState::AwaitingOutput);
    const auto flac_token=flac->capture_token;
    const auto bytes=recording_fixture::fixture();slot.kind=AudioEventKind::FlacBytes;
    slot.length=unsigned(bytes.size());memcpy(slot.bytes,bytes.data(),bytes.size());slot.time=1.1;
    ProcessFlacCapture(slot,buffers);
    uint32_t frame[8]={16,44100,2,0,16,0,0,0};slot.kind=AudioEventKind::FlacFrame;
    slot.length=32;memcpy(slot.bytes,frame,32);slot.time=1.2;ProcessFlacCapture(slot,buffers);
    assert(buffers.active.empty()&&buffers.ready.size()==1&&
           buffers.ready.back()->capture_token==flac_token);

    slot.kind=AudioEventKind::FlacBegin;slot.time=2;ProcessFlacCapture(slot,buffers);
    const auto replaced=buffers.active.at(slot.context)->capture_token;
    slot.time=2.1;ProcessFlacCapture(slot,buffers);
    const auto replacement=buffers.active.at(slot.context)->capture_token;
    assert(replaced!=replacement&&
           tracker.Binding(replaced).state==ownership::OwnershipState::Invalidated);
    slot.kind=AudioEventKind::FlacError;ProcessFlacCapture(slot,buffers);
    assert(tracker.Binding(replacement).state==ownership::OwnershipState::Invalidated);

    buffers={};buffers.ownership_tracker=&tracker;slot={};slot.context=0x3088;slot.kind=AudioEventKind::OggPage;
    slot.time=3;slot.epoch=7;slot.capture=true;
    const auto ogg=recording_fixture::ogg();std::uint64_t ogg_token=0;
    for(size_t at=0;at<ogg.size();) {
        const size_t header=27+ogg[at+26];size_t length=header;
        for(size_t index=27;index<header;++index)length+=ogg[at+index];
        slot.length=unsigned(length);memcpy(slot.bytes,ogg.data()+at,length);
        ProcessOggCapture(slot,buffers);
        if(!ogg_token&&!buffers.ogg_active.empty()) {
            const auto& capture=*buffers.ogg_active.begin()->second;
            ogg_token=capture.capture_token;assert(capture.decoder_owner==0x3000);
        }
        slot.time+=.1;at+=length;
    }
    assert(ogg_token&&buffers.ogg_active.empty()&&buffers.ready.size()==1&&
           buffers.ready.back()->capture_token==ogg_token);

    buffers={};buffers.ownership_tracker=&tracker;slot={};slot.context=0x4088;slot.kind=AudioEventKind::OggPage;
    slot.time=4;slot.epoch=7;slot.capture=true;
    const auto bos_a=recording_fixture::page(recording_fixture::identification(),0,2,0,41);
    const auto bos_b=recording_fixture::page(recording_fixture::identification(),0,2,0,42);
    slot.length=unsigned(bos_a.size());memcpy(slot.bytes,bos_a.data(),bos_a.size());ProcessOggCapture(slot,buffers);
    slot.length=unsigned(bos_b.size());memcpy(slot.bytes,bos_b.data(),bos_b.size());ProcessOggCapture(slot,buffers);
    assert(buffers.ogg_active.size()==2);
    ownership::DecodedActivity activity{};activity.owner=0x4000;activity.calls=1;
    activity.produced_floats=2;activity.first=4.1;activity.last=4.1;activity.epoch=7;
    tracker.ObserveDecoded(activity);
    for(const auto& entry:buffers.ogg_active)
        assert(tracker.Binding(entry.second->capture_token).state==ownership::OwnershipState::Conflict);

    buffers={};buffers.ownership_tracker=&tracker;slot={};slot.context=0x80;slot.kind=AudioEventKind::OggPage;
    slot.time=5;slot.epoch=7;slot.capture=true;slot.length=unsigned(bos_a.size());
    memcpy(slot.bytes,bos_a.data(),bos_a.size());ProcessOggCapture(slot,buffers);
    assert(tracker.Binding(buffers.ogg_active.begin()->second->capture_token).state==
           ownership::OwnershipState::Invalidated);

    buffers={};buffers.ownership_tracker=&tracker;slot={};slot.context=0x5000;
    slot.kind=AudioEventKind::AacBegin;slot.time=6;slot.epoch=7;ProcessAacCapture(slot,buffers);
    const auto* aac=buffers.active.at(slot.context).get();assert(aac->capture_token&&
        !aac->decoder_owner&&tracker.Binding(aac->capture_token).state==ownership::OwnershipState::Invalidated);
    decoder_ownership_layout={};decoder_ownership_layout_available=false;
}
void OwnershipDiagnosticPrivacy() {
    diagnostic_lines.clear();ownership_output_logged={};
    ownership_output_logged_count=ownership_output_logged_next=0;
    decoder_ownership_layout={};decoder_ownership_layout.flac_capture_to_decoder=0;
    decoder_ownership_layout_available=true;
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(12);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    Capture capture;capture.lossless=true;capture.born=1;
    constexpr uintptr_t raw_owner=0x1234567812345680ull;
    BeginCaptureOwnership(capture,buffers,ownership::DecoderCodec::Flac,raw_owner,0,1,12);
    buffers.active[raw_owner]=std::make_unique<Capture>(std::move(capture));
    ResetDecodedActivityQueue();QueueDecodedActivity(raw_owner,80,100,80,2,12);
    DrainDecodedActivity(tracker,12);
    tracker.ObservePlayback("private-playback-id","spotify:track:private",44,3);
    LogOwnershipBindings(buffers);
    bool saw_begin=false,saw_output=false,saw_binding=false;
    for(const auto& line:diagnostic_lines)if(line.find("ownership event=")!=std::string::npos) {
        saw_begin|=line.find("capture-begin")!=std::string::npos;
        saw_output|=line.find("output-first")!=std::string::npos;
        saw_binding|=line.find("event=binding")!=std::string::npos;
        assert(line.find("1234567812345680")==std::string::npos&&
               line.find("private-playback-id")==std::string::npos&&
               line.find("spotify:track:private")==std::string::npos);
    }
    assert(saw_begin&&saw_output&&saw_binding);
    decoder_ownership_layout={};decoder_ownership_layout_available=false;
}
void ConcurrentOggLogicalStreams() {
    CaptureBuffers buffers;Slot slot{};slot.context=0x7788;slot.kind=AudioEventKind::OggPage;
    slot.time=100;slot.epoch=4;slot.capture=true;
    const auto send=[&](const std::vector<uint8_t>& page) {
        slot.length=unsigned(page.size());memcpy(slot.bytes,page.data(),page.size());
        ProcessOggCapture(slot,buffers);slot.time+=.1;
    };
    const auto bos_a=recording_fixture::page(recording_fixture::identification(),0,2,0,41);
    const auto bos_b=recording_fixture::page(recording_fixture::identification(),0,2,0,42);
    send(bos_a);send(bos_b);
    assert(buffers.ogg_active.size()==2&&buffers.active.empty());
    const auto unknown=recording_fixture::page({0},1,0,44100,99);
    diagnostic_lines.clear();send(unknown);
    assert(buffers.ogg_active.size()==2&&buffers.ready.empty()&&Contains("inactive-stream"));
    send(recording_fixture::page({0},1,0,44100,41));
    send(recording_fixture::page({0},1,0,44100,42));
    send(recording_fixture::page({0},2,4,88200,41));
    assert(buffers.ogg_active.size()==1&&buffers.ready.size()==1);
    send(recording_fixture::page({0},2,4,88200,42));
    assert(buffers.ogg_active.empty()&&buffers.ready.size()==2);
    assert(buffers.ready[0]->stream.serial==41&&buffers.ready[1]->stream.serial==42);

    buffers={};send(bos_a);diagnostic_lines.clear();
    send(recording_fixture::page({0},2,4,88200,41));
    assert(buffers.ogg_active.empty()&&buffers.ready.empty());
    assert(Contains("sequence-discontinuity"));

    buffers={};send(bos_a);send(bos_b);
    auto damaged=recording_fixture::page({0},1,0,44100,41);damaged.back()^=1;
    send(damaged);
    assert(buffers.ogg_active.size()==1&&buffers.ogg_active.begin()->first.serial==42&&
           "a CRC failure with an exact serial/sequence must not erase an interleaved peer");
    for(const bool same_context:{false,true}) {
        buffers={};const auto bytes=recording_fixture::ogg();const double start=slot.time;
        std::vector<uint8_t> final_b;
        for(size_t at=0;at<bytes.size();) {
            const size_t header=27+bytes[at+26];size_t length=header;
            for(size_t i=27;i<header;++i)length+=bytes[at+i];
            Page parsed;assert(ParsePage(bytes.data()+at,length,parsed));
            const std::vector<uint8_t> body(bytes.begin()+at+header,bytes.begin()+at+length);
            const std::vector<uint8_t> lacing(bytes.begin()+at+27,bytes.begin()+at+header);
            const auto a=recording_fixture::page_parts(body,lacing,parsed.sequence,parsed.flags,uint64_t(parsed.granule),41);
            const auto b=recording_fixture::page_parts(body,lacing,parsed.sequence,parsed.flags,uint64_t(parsed.granule),42);
            slot.context=77;send(a);slot.context=same_context?77:78;
            if(parsed.flags&4)final_b=b;else send(b);
            at+=length;
        }
        Heard heard;heard.start=start;heard.finish=slot.time;heard.media.duration=1;
        std::vector<Heard> pending{heard};SchedulePublications(pending,buffers,0,slot.time);
        SaveJob job;assert(!saves.Pop(job)&&pending.size()==1&&buffers.ogg_active.size()==1);
        send(final_b);SchedulePublications(pending,buffers,0,slot.time);
        assert(!saves.Pop(job)&&pending.empty()&&"first EOS cannot select ownership across interleaved contexts/serials");
    }
}
void HookBoundaries() {
    queue=new Slot[capacity];event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    original_flac_init=Init;original_flac_read=Read;original_flac_frame=Frame;original_flac_error=Error;
    auto* context=reinterpret_cast<void*>(0x1234);auto* result=reinterpret_cast<void*>(0x5678);
    assert(FlacInitHook(context,result)==result&&init_calls==1);Slot slot{};assert(Pop(slot)&&slot.kind==AudioEventKind::FlacBegin);
    unsigned char buffer[16]{};size_t n=sizeof(buffer);
    assert(FlacReadHook(context,buffer,&n)==0&&n==4&&read_calls==1);
    assert(Pop(slot)&&slot.kind==AudioEventKind::FlacBytes&&slot.length==4&&!memcmp(buffer,slot.bytes,4));
    read_status=1;n=sizeof(buffer);assert(FlacReadHook(context,buffer,&n)==1&&n==0);
    assert(Pop(slot)&&slot.kind==AudioEventKind::FlacEnd);
    read_status=2;n=sizeof(buffer);assert(FlacReadHook(context,buffer,&n)==2&&n==0);
    assert(Pop(slot)&&slot.kind==AudioEventKind::FlacError&&slot.format==2&&slot.error_source==FlacErrorSource::ReadCallback&&slot.requested==16&&slot.returned==0);
    read_status=0;oversized_read=true;n=sizeof(buffer);assert(FlacReadHook(context,buffer,&n)==0&&n==17);
    assert(Pop(slot)&&slot.error_source==FlacErrorSource::ReadCallback&&slot.returned==17);assert(read_calls==4);
    uint32_t frame[8]={16,44100,2,0,16,0,0,0};int pcm=42;expected_frame=frame;expected_pcm=&pcm;
    const auto original_frame=std::vector<uint32_t>(frame,frame+8);
    assert(FlacFrameHook(context,frame,&pcm)==0&&frame_calls==1);
    assert(Pop(slot)&&slot.kind==AudioEventKind::FlacFrame&&!memcmp(slot.bytes,frame,32));
    frame_status=1;assert(FlacFrameHook(context,frame,&pcm)==1&&frame_calls==2);
    assert(Pop(slot)&&slot.error_source==FlacErrorSource::FrameCallback&&slot.format==1);
    FlacErrorHook(context,2);assert(error_calls==1);
    assert(Pop(slot)&&slot.error_source==FlacErrorSource::DecoderCallback&&slot.format==2);
    assert(pcm==42&&!memcmp(frame,original_frame.data(),32)&&!audio_callbacks.Active());
    original_decoded_process=Decode;decoded_accelerator.Reset();decoded_fallback_requested=false;
    ResetDecodedActivityQueue();decode_calls=0;
    InterlockedExchange(&history::capture_speed_value,1);InterlockedExchange(&history::capture_epoch,7);
    float samples[100]{};unsigned char compressed[200]{};
    decoded_output::Span decoded{samples,100},encoded{compressed,200};
    SetLastError(0);assert(DecodedSinkHook(context,result,&decoded,&encoded,true)==result);
    assert(decoded.data==samples+80&&decoded.size==20&&
           reinterpret_cast<unsigned char*>(encoded.data)==compressed+100&&encoded.size==100&&
           GetLastError()==1234&&decoded_accelerator.Enabled()&&!audio_callbacks.Active()&&decode_calls==1);
    DecodedActivitySlot activity{};assert(PopDecodedActivity(activity));
    assert(activity.context==reinterpret_cast<uintptr_t>(context)&&activity.calls==1&&
           activity.produced_floats==80&&activity.consumed_bytes==100&&activity.kept_floats==80&&
           activity.first==activity.last&&activity.epoch==7&&!PopDecodedActivity(activity));

    InterlockedExchange(&history::capture_speed_value,10);
    decoded={samples,100};encoded={compressed,200};
    SetLastError(0);assert(DecodedSinkHook(context,result,&decoded,&encoded,true)==result);
    assert(decoded.data==samples+8&&decoded.size==92&&GetLastError()==1234&&decode_calls==2);
    assert(PopDecodedActivity(activity)&&activity.produced_floats==80&&
           activity.consumed_bytes==100&&activity.kept_floats==8&&activity.epoch==7);
    invalid_decoded_span=true;decoded={samples,100};encoded={compressed,200};
    const auto activity_loss=decoded_activity_dropped;
    DecodedSinkHook(context,result,&decoded,&encoded,true);
    assert(decoded.data==samples+79&&decoded.size==20&&!decoded_accelerator.Enabled()&&
           decoded_fallback_requested.load()&&decoded_activity_dropped==activity_loss+1&&
           !PopDecodedActivity(activity)&&decode_calls==3);
    ownership::DecoderOwnershipTracker invalid_tracker;invalid_tracker.Reset(7);
    const auto invalid_capture=invalid_tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x1000,0,Now(),7);
    DrainDecodedActivity(invalid_tracker,7);
    assert(invalid_tracker.Binding(invalid_capture).state==ownership::OwnershipState::Invalidated);
    decoded_accelerator.Reset();decoded_fallback_requested=false;invalid_decoded_span=false;
    InterlockedExchange(&history::capture_speed_value,1);InterlockedExchange(&history::debug_enabled,0);
    decoded={samples,100};encoded={compressed,200};DecodedSinkHook(context,result,&decoded,&encoded,true);
    assert(PopDecodedActivity(activity)&&activity.produced_floats==80&&activity.kept_floats==80);
    InterlockedExchange(&history::debug_enabled,1);
    invalid_decoded_span=false;InterlockedExchange(&history::capture_speed_value,1);
    diagnostic_lines.clear();LogTiming();
    assert(diagnostic_lines.empty()&&native_read_timing.count.load()==0&&native_frame_timing.count.load()==0&&producer_wait_timing.count.load()==0);
    diagnostic_lines.clear();LogTiming();assert(diagnostic_lines.empty());
    oversized_read=false;empty_read=true;read_status=0;n=sizeof(buffer);
    assert(FlacReadHook(context,buffer,&n)==0&&n==0&&!Pop(slot)&&empty_flac_reads.load()==0);
    InterlockedExchange(&history::networking_enabled,1);InterlockedExchange(&history::hook_diagnostics_enabled,1);
    n=sizeof(buffer);assert(FlacReadHook(context,buffer,&n)==0&&n==0&&!Pop(slot));
    assert(empty_flac_reads.load()==1&&native_read_timing.count.load()==1);
    LogTiming();assert(Contains("hook timing")&&Contains("empty_reads=1")&&empty_flac_reads.load()==0);
    diagnostic_lines.clear();empty_read=false;
    InterlockedExchange(&history::networking_enabled,0);InterlockedExchange(&history::hook_diagnostics_enabled,0);
    CloseHandle(event);event=nullptr;delete[] queue;queue=nullptr;
}
DWORD WINAPI QueueDecodedThread(void* argument) {
    const auto context=reinterpret_cast<uintptr_t>(argument);
    for(unsigned index=0;index<50;++index)
        QueueDecodedActivity(context,2,1,2,100+index*.001,9);
    return 0;
}
void DecodedActivityQueue() {
    ResetDecodedActivityQueue();
    QueueDecodedActivity(0x1000,80,100,8,1.0,7);
    QueueDecodedActivity(0x1000,40,50,4,1.1,7);
    DecodedActivitySlot slot{};assert(PopDecodedActivity(slot));
    assert(slot.context==0x1000&&slot.calls==2&&slot.produced_floats==120&&
           slot.consumed_bytes==150&&slot.kept_floats==12&&slot.first==1.0&&slot.last==1.1);
    QueueDecodedActivity(0x1000,1,1,1,2.0,7);
    QueueDecodedActivity(0x2000,1,1,1,2.1,7);
    QueueDecodedActivity(0x2000,1,1,1,2.2,8);
    assert(decoded_activity_count==3);
    while(PopDecodedActivity(slot)) {}

    QueueDecodedActivity(0x3000,1,1,1,3.0,7);
    auto& saturated=decoded_activity_queue[decoded_activity_head];
    saturated.calls=saturated.produced_floats=saturated.consumed_bytes=
        saturated.kept_floats=std::numeric_limits<uint64_t>::max();
    QueueDecodedActivity(0x3000,1,1,1,3.1,7);
    assert(PopDecodedActivity(slot)&&slot.calls==std::numeric_limits<uint64_t>::max()&&
           slot.produced_floats==std::numeric_limits<uint64_t>::max()&&
           slot.consumed_bytes==std::numeric_limits<uint64_t>::max()&&
           slot.kept_floats==std::numeric_limits<uint64_t>::max());

    ResetDecodedActivityQueue();const auto compressed_loss=dropped;
    for(unsigned index=0;index<decoded_activity_capacity;++index)
        QueueDecodedActivity(0x1000+index*16,2,1,2,4.0+index*.001,7);
    QueueDecodedActivity(0x5000,2,1,2,5.0,7);
    assert(decoded_activity_count==decoded_activity_capacity&&decoded_activity_dropped==1&&
           dropped==compressed_loss);
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(7);
    const auto capture=tracker.BeginCapture(ownership::DecoderCodec::Flac,0x1000,0,3.9,7);
    DrainDecodedActivity(tracker,7);
    assert(tracker.Binding(capture).state==ownership::OwnershipState::Invalidated&&
           decoded_activity_loss_seen==decoded_activity_dropped);

    ResetDecodedActivityQueue();
    HANDLE first=CreateThread(nullptr,0,QueueDecodedThread,reinterpret_cast<void*>(0x6000),0,nullptr);
    HANDLE second=CreateThread(nullptr,0,QueueDecodedThread,reinterpret_cast<void*>(0x7000),0,nullptr);
    assert(first&&second);HANDLE threads[]={first,second};
    assert(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    CloseHandle(first);CloseHandle(second);
    std::uint64_t calls=0;while(PopDecodedActivity(slot))calls+=slot.calls;
    assert(calls==100&&decoded_activity_dropped==0);
}
void TimingVerbosity() {
    LARGE_INTEGER frequency{};assert(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0);
    const auto ticks=uint64_t(frequency.QuadPart);
    InterlockedExchange(&history::networking_enabled,1);InterlockedExchange(&history::hook_diagnostics_enabled,1);
    LogTiming();diagnostic_lines.clear(); // Drain earlier fixtures.
    native_read_timing.Add(ticks/1000);native_frame_timing.Add(ticks/500);
    producer_wait_timing.Add(ticks/1000);
    LogTiming();assert(diagnostic_lines.empty()); // Routine sub-threshold timing is silent.
    native_frame_timing.Add(ticks/1000);
    double deadline=20;DrainTiming(19,deadline);assert(native_frame_timing.count.load()==1&&deadline==20);
    DrainTiming(20,deadline);assert(native_frame_timing.count.load()==0&&deadline==30&&diagnostic_lines.empty());
    DrainTiming(21,deadline);assert(deadline==30&&diagnostic_lines.empty());
    producer_wait_timing.Add(ticks/100+1);
    LogTiming();assert(diagnostic_lines.size()==1&&Contains("reason=slow")&&Contains("worst=queue-producer-wait"));
    diagnostic_lines.clear();LogTiming();assert(diagnostic_lines.empty());
    empty_flac_reads.store(1);LogTiming();
    assert(diagnostic_lines.size()==1&&Contains("reason=empty-read")&&Contains("empty_reads=1"));
    diagnostic_lines.clear();InterlockedExchange(&history::debug_enabled,0);
    LogTiming();assert(diagnostic_lines.empty());
    InterlockedExchange(&history::debug_enabled,1);
    InterlockedExchange(&history::networking_enabled,0);InterlockedExchange(&history::hook_diagnostics_enabled,0);
}
void BasicModeProfiling() {
    InterlockedExchange(&history::log_enabled,1);InterlockedExchange(&history::debug_enabled,1);
    InterlockedExchange(&history::networking_enabled,0);InterlockedExchange(&history::hook_diagnostics_enabled,0);
    for(auto* sample:{&native_read_timing,&native_frame_timing,&native_error_timing,&native_init_timing,
                     &native_ogg_timing,&producer_wait_timing,&producer_hold_timing,&consumer_hold_timing}) {
        sample->count.store(0);sample->maximum.store(0);
    }
    {
        NativeTimer timer(native_read_timing);assert(timer.began==0);timer.Finish();
        QueueGuard guard;assert(guard.began==0&&guard.acquired==0);
    }
    assert(native_read_timing.count.load()==0&&producer_wait_timing.count.load()==0&&producer_hold_timing.count.load()==0);
    diagnostic_lines.clear();LogTiming();assert(diagnostic_lines.empty());
}
void LoggingDisabled() {
    InterlockedExchange(&history::log_enabled,0);InterlockedExchange(&history::debug_enabled,1);
    diagnostic_lines.clear();
    {
        NativeTimer timer(native_read_timing);assert(timer.began==0);timer.Finish();
        QueueGuard guard;assert(guard.began==0&&guard.acquired==0);
    }
    assert(native_read_timing.count.load()==0&&producer_wait_timing.count.load()==0);
    PlaybackDiagnostics playback;Media media;media.title=L"Song";media.playing=true;media.playback_status=4;
    playback.Observe(media,10,1);LogTiming();
    assert(diagnostic_lines.empty()&&!playback.known);
    InterlockedExchange(&history::log_enabled,1);
}
void AppIdentityAndSourceChanges() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.title=L"Same title";media.artist=L"Artist";media.album=L"Album";media.duration=10;media.playing=true;
    for(unsigned i=0;i<10;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,100+i);}
    // Metadata differences do not change URI identity, and different URIs do.
    media.title=L"Refreshed label";media.position=9.5;AssociateListen(media,listen,current,heard,buffers,109.5);
    assert(listen.eligible&&heard.empty());
    media.spotify_uri="spotify:track:0000000000000000000002";media.position=0;
    AssociateListen(media,listen,current,heard,buffers,110);
    assert(heard.size()==1&&heard[0].media.spotify_uri=="spotify:track:0000000000000000000001");
    heard.clear();listen={};current={};media={};media.title=L"Windows track";media.duration=10;media.playing=true;
    for(unsigned i=0;i<10;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,200+i);}
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";media.position=9.9;
    AssociateListen(media,listen,current,heard,buffers,210);
    assert(heard.empty()&&!listen.eligible); // Never stitch a source change into a complete listen.
}
void NaturalAppBufferingTransition() {
    const auto run=[](double transition_time,bool expected) {
        CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
        media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
        media.playback_instance="first";media.title=L"Previous";media.duration=10;media.playing=true;
        for(unsigned i=0;i<=19;++i){media.position=i*.5;AssociateListen(media,listen,current,heard,buffers,100+i*.5);}
        Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:0000000000000000000002";next.playback_instance="second";
        FinishAppTransition(next,listen,current,heard,buffers,transition_time);
        assert(heard.size()==size_t(expected)&&!listen.eligible);
        FinishAppTransition(next,listen,current,heard,buffers,transition_time+.2);assert(heard.size()==size_t(expected));
        next.title=L"Next";next.duration=20;next.position=.2;next.playing=true;
        AssociateListen(next,listen,current,heard,buffers,transition_time+.2);assert(listen.eligible);
    };
    run(110,true);run(109.6,false); // Time spent buffering cannot manufacture the missing tail.
}
std::unique_ptr<Capture> CompleteFlacCapture(double born,double finished) {
    auto capture=std::make_unique<Capture>();capture->lossless=true;capture->born=born;capture->finished=finished;
    const auto bytes=recording_fixture::fixture(1,16);
    assert(capture->data.Append(bytes.data(),bytes.size(),bytes.size()+65536));capture->bytes=bytes.size();
    size_t audio=0;assert(ParseFlacMetadata(capture->data,capture->flac,audio)==FlacParse::Valid);
    capture->metadata=true;capture->coverage.samples=16;capture->coverage.frames=1;return capture;
}
void FinalCaptureRevalidation() {
    auto valid=CompleteFlacCapture(100,111);
    assert(ValidateFinalCapture(*valid)==FinalCaptureValidation::Valid);
    auto corrupt=CompleteFlacCapture(100,111);corrupt->flac.total_samples=17;
    assert(ValidateFinalCapture(*corrupt)==FinalCaptureValidation::Invalid);
    auto truncated=CompleteFlacCapture(100,111);truncated->coverage.samples=15;
    assert(ValidateFinalCapture(*truncated)==FinalCaptureValidation::Incomplete);
    const auto ogg_bytes=recording_fixture::ogg();
    auto ogg=[](const std::vector<uint8_t>& bytes) {
        auto capture=std::make_unique<Capture>();capture->born=100;capture->finished=111;
        assert(capture->data.Append(bytes.data(),bytes.size(),bytes.size()+65536));capture->bytes=bytes.size();
        capture->stream.complete=true;capture->stream.serial=42;capture->stream.rate=44100;
        capture->stream.channels=2;capture->stream.pages=9;capture->stream.samples=44100;return capture;
    };
    auto valid_ogg=ogg(ogg_bytes);assert(ValidateFinalCapture(*valid_ogg)==FinalCaptureValidation::Valid);
    auto corrupt_ogg_bytes=ogg_bytes;corrupt_ogg_bytes.back()^=1;
    auto corrupt_ogg=ogg(corrupt_ogg_bytes);assert(ValidateFinalCapture(*corrupt_ogg)==FinalCaptureValidation::Invalid);
}
void CrossfadeQuarantineRecovery() {
    const auto run=[](bool explicit_skip,bool ambiguous,bool expire) {
        CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
        media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
        media.playback_instance="first";media.title=L"Previous";media.duration=16;media.playing=true;
        for(unsigned i=0;i<=4;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,100+i);}
        auto active=CompleteFlacCapture(100,0);active->coverage.samples=8;active->finished=0;
        buffers.active[1]=std::move(active);
        Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:0000000000000000000002";
        next.playback_instance="second";next.explicit_skip=explicit_skip;next.natural_transition=true;
        FinishAppTransition(next,listen,current,heard,buffers,105);
        if(explicit_skip) {
            assert(heard.empty()&&!listen.eligible);return;
        }
        assert(heard.size()==1&&heard[0].provisional&&heard[0].deadline==120);
        assert(buffers.active.size()==1&&buffers.active[1]->retained_until==120);
        auto completed=std::move(buffers.active[1]);buffers.active.erase(1);
        completed->coverage.samples=16;completed->finished=expire?121:110;
        buffers.ready.push_back(std::move(completed));
        if(ambiguous)buffers.ready.push_back(CompleteFlacCapture(100,110));
        SchedulePublications(heard,buffers,0,expire?121:110);SaveJob job;
        assert(saves.Pop(job)==(!ambiguous&&!expire));
        if(!ambiguous&&!expire)assert(job.heard.provisional&&ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid);
    };
    run(false,false,false);run(false,false,true);run(false,true,false);run(true,false,false);
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.playback_instance="early";media.title=L"Early EOS";media.duration=16;media.playing=true;
    for(unsigned i=0;i<=4;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,100+i);}
    auto early=CompleteFlacCapture(100,104);early->coverage.samples=16;buffers.ready.push_back(std::move(early));
    Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:0000000000000000000002";
    next.playback_instance="next";next.natural_transition=true;FinishAppTransition(next,listen,current,heard,buffers,105);
    SchedulePublications(heard,buffers,0,105);SaveJob early_job;
    assert(saves.Pop(early_job)&&early_job.heard.provisional&&early_job.capture->finished==104&&
           "a uniquely matched, revalidated complete buffer survives a false natural transition flag");
    for(unsigned scenario=0;scenario<6;++scenario) {
        CaptureBuffers rejected;std::vector<Heard> pending;Listen trial;Media previous;
        media.position=0;media.playing=true;
        for(unsigned i=0;i<=4;++i){media.position=i;AssociateListen(media,trial,previous,pending,rejected,100+i);}
        rejected.ready.push_back(CompleteFlacCapture(100,104));
        auto blocked=media;blocked.position=scenario==0?13:scenario==1?16:4;
        blocked.natural_transition=true;
        if(scenario>=3){blocked.playback_instance="new";blocked.natural_transition=scenario!=3;}
        if(scenario==2)trial.playing=false;
        if(scenario==4)blocked.explicit_skip=true;
        FinishAppTransition(blocked,trial,previous,pending,rejected,scenario==5?110:105);
        SchedulePublications(pending,rejected,0,110);SaveJob rejected_job;
        assert(pending.empty()&&!saves.Pop(rejected_job)&&"seek, pause, unknown/manual or stale transition cannot recover a listen");
    }
}
void AcceleratedUiClockRecovery() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current;
    current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="accelerated-first";current.title=L"Accelerated";
    current.duration=16;current.position=0;current.playing=true;
    assert(listen.Observe(current.ListenKey(),0,16,true,100,10).empty());
    // The decoder reaches EOS at 10x while Spotify's public position remains
    // near its old 1x value. The complete cached source is the final evidence.
    assert(listen.Observe(current.ListenKey(),0,16,true,101.6,10).empty());
    buffers.ready.push_back(CompleteFlacCapture(100,101.5));
    auto preload=std::make_unique<Capture>();preload->lossless=true;preload->metadata=true;
    preload->born=100.5;preload->flac.rate=1;preload->flac.total_samples=20;
    buffers.active[2]=std::move(preload);
    Media next;next.source=MediaSource::Spotify;
    next.spotify_uri="spotify:track:0000000000000000000002";
    next.playback_instance="accelerated-second";next.natural_transition=true;
    FinishAppTransition(next,listen,current,heard,buffers,101.6);
    assert(heard.size()==1&&heard[0].provisional&&
           "a unique complete accelerated capture survives Spotify's 1x public clock");
    SchedulePublications(heard,buffers,0,101.6);SaveJob job;
    assert(saves.Pop(job)&&ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid);
}
void KnownDurationAlternativeIsNotAmbiguous() {
    for(bool discarded_before_matching:{false,true}) {
        CaptureBuffers buffers;std::vector<Heard> heard(1);
        heard[0].media.duration=16;heard[0].start=100;heard[0].finish=116;
        heard[0].generation=1;buffers.ready.push_back(CompleteFlacCapture(100,115));
        auto alternative=std::make_unique<Capture>();alternative->lossless=true;
        alternative->metadata=true;alternative->born=100.5;
        alternative->flac.rate=1;alternative->flac.total_samples=20;
        buffers.active[2]=std::move(alternative);
        if(discarded_before_matching) {
            buffers.RecordLostActive(buffers.active[2].get());buffers.active.erase(2);
        }
        SchedulePublications(heard,buffers,0,116);SaveJob job;
        assert(saves.Pop(job)&&job.heard.generation==1&&
               "a known different-duration stream cannot make ownership ambiguous");
    }
}
void RecentPlaybackMatching() {
    RecentPlaybackHistory history;Media media;media.source=MediaSource::Spotify;
    media.spotify_uri="spotify:track:0000000000000000000001";media.playback_instance="one";media.duration=10;
    Listen listen;listen.identity=media.ListenKey();listen.generation=1;listen.start_time=100;
    history.Observe(media,listen);Heard completed{media,100,110,1};history.Complete(completed);
    media.title=L"Delayed title";media.artist=L"Artist";media.album=L"Album";
    history.Observe(media,listen);history.Refresh(completed);assert(completed.media.title==L"Delayed title");
    for(unsigned i=2;i<=7;++i){media.playback_instance=std::to_string(i);listen.identity=media.ListenKey();listen.generation=i;listen.start_time=100+i*10;history.Observe(media,listen);}
    assert(history.Size()==5&&!history.Find(1,completed.media.ListenKey()));
    history.Clear();assert(history.Size()==0);
}
void FinalReleaseReconciliation() {
    const auto heard=[] {
        Heard h;h.start=100;h.finish=110;h.media.duration=16;h.media.title=L"Late metadata";
        h.generation=1;h.media.source=MediaSource::Spotify;
        h.media.spotify_uri="spotify:track:0000000000000000000001";h.media.playback_instance="final-check";
        h.provisional=true;h.deadline=120;return h;
    };
    {
        CaptureBuffers buffers;buffers.ready.push_back(CompleteFlacCapture(100,110));
        auto h=heard();RecentPlaybackHistory recent;recent.Complete(h);
        auto refreshed=h.media;refreshed.title=L"Refreshed title";
        Listen listen;listen.generation=h.generation;listen.identity=h.media.ListenKey();recent.Observe(refreshed,listen);
        std::vector<Heard> pending{h};SaveJob job;
        SchedulePublications(pending,buffers,0,121,&recent);
        assert(saves.Pop(job)&&"timely complete capture gets one last match before its pending record expires");
        assert(job.heard.media.title==L"Refreshed title");
        publishing_reserved.fetch_sub(job.reserved);
    }
    {
        CaptureBuffers buffers;
        for(unsigned i=0;i<6;++i)buffers.ready.push_back(CompleteFlacCapture(100+i*100,110+i*100));
        auto h=heard();h.provisional=false;std::vector<Heard> pending{h};SaveJob job;
        SchedulePublications(pending,buffers,0,112);
        assert(saves.Pop(job)&&job.capture->born==100&&"reconciliation precedes capacity eviction");
        publishing_reserved.fetch_sub(job.reserved);
    }
    {
        SaveJob blocker;assert(saves.Push(std::move(blocker)));assert(saves.Push(SaveJob{}));
        CaptureBuffers buffers;buffers.ready.push_back(CompleteFlacCapture(100,110));
        std::vector<Heard> pending{heard()};SchedulePublications(pending,buffers,0,121);
        assert(pending.size()==1&&buffers.ready.size()==1&&buffers.ready[0]->data.size()>0);
        SaveJob job;assert(saves.Pop(job)&&saves.Pop(job));
        SchedulePublications(pending,buffers,0,122);
        assert(saves.Pop(job)&&pending.empty()&&buffers.ready.empty());
        publishing_reserved.fetch_sub(job.reserved);
    }
    {
        auto valid=CompleteFlacCapture(100,110);
        assert(std::string(CheckBeforeRelease(*valid,"test")).find("no verified metadata/listen match")!=std::string::npos);
        auto incomplete=CompleteFlacCapture(100,110);incomplete->coverage.samples=8;
        assert(std::string(CheckBeforeRelease(*incomplete,"test"))=="complete valid audio; capture/listen verification failed");
        auto truncated=CompleteFlacCapture(100,110);truncated->coverage.samples=8;
        auto prefix=truncated->data.Flatten();prefix.resize(prefix.size()-2);truncated->bytes=prefix.size();
        assert(truncated->data.Append(prefix.data(),prefix.size(),65536));
        assert(std::string(CheckBeforeRelease(*truncated,"test"))=="incomplete or invalid cached audio");
        auto bad=CompleteFlacCapture(100,110);auto bytes=bad->data.Flatten();bytes.back()^=1;
        assert(bad->data.Append(bytes.data(),bytes.size(),65536));
        assert(std::string(CheckBeforeRelease(*bad,"test"))=="invalid cached audio");
    }
}
void DurableAssociationEvidence() {
    {
        // A corrupt competing stream can be lost in the same drained batch,
        // before the scheduler ever sees it alongside the ready recording.
        CaptureBuffers batch;std::vector<Heard> pending;
        batch.ready.push_back(CompleteFlacCapture(100,110));
        Heard heard;heard.start=100;heard.finish=110;heard.media.duration=16;pending.push_back(heard);
        Slot event{};event.context=99;event.time=100.1;event.kind=AudioEventKind::FlacBegin;
        ProcessFlacCapture(event,batch);event.kind=AudioEventKind::FlacError;ProcessFlacCapture(event,batch);
        SchedulePublications(pending,batch,0,111);SaveJob job;
        assert(!saves.Pop(job)&&pending.empty()&&"lost same-batch active alternatives leave durable ownership evidence");
    }
    {
        CaptureBuffers interleaved;std::vector<Heard> pending;
        auto completed=CompleteFlacCapture(100,110);
        Heard heard;heard.start=100;heard.finish=110;heard.media.duration=16;heard.provisional=true;heard.deadline=125;
        pending.push_back(heard);interleaved.ready.push_back(std::move(completed));
        auto active=std::make_unique<Capture>();active->born=100.1;
        interleaved.ogg_active[{77,42}]=std::move(active);
        for(unsigned pass=0;pass<10;++pass)SchedulePublications(pending,interleaved,0,111);
        SaveJob job;assert(!saves.Pop(job)&&pending.size()==1&&interleaved.ogg_active.size()==1);
        assert(interleaved.ready[0]->validation_runs==1&&"immutable completed capture validated once across matching passes");
        interleaved.ogg_active.clear();SchedulePublications(pending,interleaved,0,112);
        assert(!saves.Pop(job)&&pending.empty()&&"removing an active alternative cannot establish ownership");
        auto wrong_duration=CompleteFlacCapture(100,110);heard.media.duration=200;
        assert(!CaptureEvidenceMatchesListen(*wrong_duration,heard)&&wrong_duration->validation_runs==0);
    }
    const auto capture=[](double born,double finished) {
        auto value=std::make_unique<Capture>();value->lossless=true;value->born=born;value->finished=finished;
        value->flac.rate=1;value->coverage.samples=10;return value;
    };
    Heard one;one.media.title=L"One";one.media.duration=10;one.start=100;one.finish=110;one.generation=1;
    Heard two=one;two.media.title=L"Two";two.start=101;two.finish=111;two.generation=2;
    CaptureBuffers buffers;buffers.ready.push_back(capture(100,110));std::vector<Heard> pending{one,two};
    SchedulePublications(pending,buffers,0,112);SaveJob job;assert(!saves.Pop(job));
    SchedulePublications(pending,buffers,0,711);assert(!saves.Pop(job)); // Expiry must not prove ownership.
    buffers={};pending={one};buffers.ready.push_back(capture(100,110));buffers.ready.push_back(capture(100,111));
    SchedulePublications(pending,buffers,0,112);assert(!saves.Pop(job));
    buffers.ready.erase(buffers.ready.begin());SchedulePublications(pending,buffers,0,113);assert(!saves.Pop(job));
    buffers={};pending.clear();for(unsigned i=0;i<6;++i)buffers.ready.push_back(capture(100+i*100,110+i*100));
    SchedulePublications(pending,buffers,0,112);assert(buffers.ready.size()==5);
    one.start=110;one.finish=120;pending={one};SchedulePublications(pending,buffers,0,121);assert(!saves.Pop(job));
}
void AppTailDeadline() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.title=L"Previous";media.duration=10;media.playing=true;
    for(unsigned i=0;i<=19;++i){media.position=i*.5;AssociateListen(media,listen,current,heard,buffers,100+i*.5);}
    auto active=std::make_unique<Capture>();active->lossless=true;active->born=100;active->flac.rate=1;active->coverage.samples=9;buffers.active[1]=std::move(active);
    Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:0000000000000000000002";
    FinishAppTransition(next,listen,current,heard,buffers,110);
    assert(heard.size()==1&&buffers.active[1]->retained_until==120);
    auto late=std::move(buffers.active[1]);buffers.active.erase(1);late->finished=121;late->coverage.samples=10;buffers.ready.push_back(std::move(late));
    SchedulePublications(heard,buffers,0,121);SaveJob job;assert(!saves.Pop(job));
}
void MissingPlaybackWarning() {
    CaptureBuffers buffers;auto complete=std::make_unique<Capture>();complete->finished=10;
    buffers.ready.push_back(std::move(complete));MediaDiagnostic diagnostic;
    diagnostic.stage="select-session";diagnostic.reason="no-spotify-session";
    diagnostic_lines.clear();
    ReportUnverifiedCaptures(buffers,false,diagnostic,20);assert(diagnostic_lines.empty());
    ReportUnverifiedCaptures(buffers,true,diagnostic,12);assert(diagnostic_lines.empty());
    ReportUnverifiedCaptures(buffers,true,diagnostic,13);
    assert(Contains("complete Ogg capture")&&Contains("not saved"));
    assert(Contains("Show desktop overlay when using media keys"));
    const auto once=diagnostic_lines.size();
    ReportUnverifiedCaptures(buffers,true,diagnostic,50);assert(diagnostic_lines.size()==once);
    MediaWarningState warning;diagnostic_lines.clear();
    ReportMediaUnavailable(buffers,true,diagnostic,100,warning);
    assert(diagnostic_lines.empty()); // Completed-only outcome already owns its failure.
    buffers.active[1]=std::make_unique<Capture>();
    ReportMediaUnavailable(buffers,true,diagnostic,100,warning);
    ReportMediaUnavailable(buffers,true,diagnostic,100.328,warning);
    assert(!warning.reported&&diagnostic_lines.empty());
    ReportMediaUnavailable(buffers,false,diagnostic,100.5,warning);
    assert(warning.since<0); // Normal startup recovered; no warning was needed.
    ReportMediaUnavailable(buffers,true,diagnostic,110,warning);
    ReportMediaUnavailable(buffers,true,diagnostic,112.9,warning);assert(diagnostic_lines.empty());
    ReportMediaUnavailable(buffers,true,diagnostic,113,warning);
    assert(warning.reported&&Contains("playback verification unavailable")&&Contains("Show desktop overlay when using media keys"));
    const auto outage=diagnostic_lines.size();
    ReportMediaUnavailable(buffers,true,diagnostic,150,warning);assert(diagnostic_lines.size()==outage);
    ReportMediaUnavailable(buffers,false,diagnostic,151,warning);assert(!warning.reported);
    ReportMediaUnavailable(buffers,true,diagnostic,160,warning);
    ReportMediaUnavailable(buffers,true,diagnostic,163,warning);assert(diagnostic_lines.size()==outage+1);

}
void Connectivity() {
    assert(!LastConnectivity().known);
    RecordConnectivity({true,true,0,0x20,0x40,10});auto value=LastConnectivity();
    assert(value.known&&value.flags_valid&&value.original==0x20&&value.effective==0x40&&value.observed_ms==10);
    RecordConnectivity({true,false,std::int32_t(0x80004005u),0,0,20});value=LastConnectivity();
    assert(value.known&&!value.flags_valid&&value.result<0);
    assert(!connectivity_observation_lock.test_and_set());
    assert(!LastConnectivity().known);RecordConnectivity({}); // Contention never blocks.
    connectivity_observation_lock.clear();assert(LastConnectivity().known);
    diagnostic_lines.clear();LogConnectivity();assert(Contains("flags_known=0")&&Contains("stream reachability unknown"));
    const auto unchanged=diagnostic_lines.size();LogConnectivity();assert(diagnostic_lines.size()==unchanged);
    RecordConnectivity({true,false,std::int32_t(0x80004005u),0,0,30});
    LogConnectivity();assert(diagnostic_lines.size()==unchanged); // Fresh query, same result: no repeated evidence.
    connectivity_observation_lock.test_and_set();LogConnectivity();connectivity_observation_lock.clear();
    assert(diagnostic_lines.size()==unchanged); // Unknown/contended snapshots are not new network facts.
}
}
namespace history {
void CaptureLogEvent(LogKind kind,const char*,const std::string& message){
    if(!LoggingEnabled()||(kind==LogKind::Diagnostic&&!DebugLoggingEnabled()))return;
    diagnostic_lines.push_back(message);if(kind==LogKind::Failure)++failure_events;
}
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media&,Catalog&){}
void EnrichTags(const Media&,Tags&){}
}
int wmain() {
    // Use in-memory preferences only. Never read or write the user's INI/music.
    InterlockedExchange(&history::debug_enabled,1);
    Connectivity();ConcurrentOggLogicalStreams();FinalCaptureRevalidation();CrossfadeQuarantineRecovery();AcceleratedUiClockRecovery();KnownDurationAlternativeIsNotAmbiguous();AppIdentityAndSourceChanges();NaturalAppBufferingTransition();RecentPlaybackMatching();FinalReleaseReconciliation();DurableAssociationEvidence();AppTailDeadline();MissingPlaybackWarning();ObservePlayback();CoverageAndFailures();CaptureOwnershipLifecycle();OwnershipDiagnosticPrivacy();HookBoundaries();DecodedActivityQueue();TimingVerbosity();BasicModeProfiling();LoggingDisabled();
    puts("PASS: observed transitions, one FLAC failure context/outcome, original callback semantics, profiling-only intervals, basic-mode zero profiling and changed-only NLM observations");
    return 0;
}
