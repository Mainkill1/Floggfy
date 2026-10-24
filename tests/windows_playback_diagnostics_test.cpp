// Exercise real hooks/worker diagnostics without patching or controlling Spotify.
#define HistoryLog CaptureHistoryLog
#include "../native/audio_history.cpp"
#undef HistoryLog
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include <cassert>

namespace {
std::vector<std::string> diagnostics;
bool Contains(const char* text) {
    return std::any_of(diagnostics.begin(),diagnostics.end(),[text](const auto& line){return line.find(text)!=std::string::npos;});
}
unsigned init_calls=0,read_calls=0,frame_calls=0,error_calls=0;
int read_status=0,frame_status=0;
bool oversized_read=false;
const void* expected_frame=nullptr;
const void* expected_pcm=nullptr;
void* Init(void* context,void* result) {
    ++init_calls;assert(context==reinterpret_cast<void*>(0x1234));
    assert(count==1&&queue[head].kind==AudioEventKind::FlacBegin);return result;
}
int Read(void* context,unsigned char* buffer,size_t* size) {
    ++read_calls;assert(context==reinterpret_cast<void*>(0x1234));
    if(oversized_read){++*size;return read_status;}
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
void ObservePlayback() {
    PlaybackDiagnostics state;Media media;media.title=L"Song";media.artist=L"Artist";
    media.position=1;media.duration=100;media.playing=true;media.playback_status=4;
    diagnostics.clear();state.Observe(media,10,1);assert(Contains("baseline")&&Contains("initiator=unknown"));
    assert(StatusDue(10,0,media,false));assert(!StatusDue(10,20,media,false));assert(!StatusDue(10,0,media,true));
    media.position=1.5;state.Observe(media,10.5,2);const auto before=diagnostics.size();
    state.Observe(media,11,3);assert(diagnostics.size()==before);
    media.playing=false;media.playback_status=5;state.Observe(media,11.5,4);assert(Contains("state=paused"));
    const auto paused=diagnostics.size();
    for(unsigned i=0;i<12;++i)state.Observe(media,12+i,5+i);
    assert(diagnostics.size()==paused&&!StatusDue(1000,0,media,false));
    media.playback_status=3;state.Observe(media,25,18);assert(Contains("state=stopped"));
    media.playback_status=2;state.Observe(media,25.5,19);assert(Contains("state=changing"));
    media.playing=true;media.playback_status=4;state.Observe(media,26,20);
    assert(StatusDue(26,20,media,false)); // Deadline stayed overdue during pause.
    media.position=0;state.Observe(media,26.5,21);assert(Contains("timeline-jump"));
    media.title=L"Next";state.Observe(media,27,22);assert(Contains("track_changed=1"));
    InterlockedExchange(&history::debug_enabled,0);const auto off=diagnostics.size();
    state.Observe(media,27.5,23);assert(diagnostics.size()==off&&!StatusDue(30,0,media,false));
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
    diagnostics.clear();ProcessFlacCapture(slot,buffers);
    assert(buffers.active.empty()&&buffers.ready.empty());
    assert(Contains("origin=capture-coverage-guard")&&Contains("reason=format")&&Contains("native_frame_result=0"));
    assert(Contains("expected_rate=44100 got_rate=48000")&&Contains("ctx=1234")&&Contains("queue_lag_ms="));
    // Native errors remain visible even after our capture has already ended.
    slot.kind=AudioEventKind::FlacError;slot.error_source=FlacErrorSource::ReadCallback;slot.format=2;slot.requested=8192;
    diagnostics.clear();ProcessFlacCapture(slot,buffers);assert(Contains("origin=native-read-return code=2 requested=8192 returned=0"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacFrame;memcpy(slot.bytes,frame,32);diagnostics.clear();ProcessFlacCapture(slot,buffers);
    assert(Contains("reason=metadata-not-ready"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    InterlockedExchange(&history::debug_enabled,0);diagnostics.clear();
    slot.kind=AudioEventKind::FlacFrame;ProcessFlacCapture(slot,buffers);
    assert(diagnostics.size()==1&&Contains("stream discarded")); // Keep normal failure activity.
    InterlockedExchange(&history::debug_enabled,1);
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
    diagnostics.clear();LogTiming();assert(Contains("metric=native-read")&&Contains("metric=native-frame")&&Contains("metric=queue-producer-wait")&&Contains("metric=queue-consumer-hold"));
    diagnostics.clear();LogTiming();assert(diagnostics.empty());
    CloseHandle(event);event=nullptr;delete[] queue;queue=nullptr;
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
    diagnostics.clear();LogConnectivity();assert(Contains("flags_known=0")&&Contains("stream reachability unknown"));
}
}
namespace history {
void CaptureHistoryLog(const char* message){diagnostics.emplace_back(message);}
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media&,Catalog&){}
void EnrichTags(const Media&,Tags&){}
}
int wmain() {
    // Use in-memory preferences only. Never read or write the user's INI/music.
    InterlockedExchange(&history::debug_enabled,1);
    Connectivity();ObservePlayback();CoverageAndFailures();HookBoundaries();
    puts("PASS: paused dedup/resume, observed transitions, exact coverage reasons, original callback semantics, bounded hook metrics and NLM observations");
    return 0;
}
