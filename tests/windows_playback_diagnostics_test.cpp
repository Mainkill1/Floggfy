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
    assert(diagnostic_lines.size()==2&&failure_events==prior_failures+1); // One context plus one normal outcome.
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
    assert(diagnostic_lines.size()==1&&Contains("stream discarded")); // Debug-off normal failure remains visible.
    InterlockedExchange(&history::debug_enabled,1);
}
void OggRejectionEvidence() {
    const auto exercise=[&](const char* reason,const std::vector<std::vector<uint8_t>>& prefix,const std::vector<uint8_t>& rejected,bool inactive=false) {
        for(unsigned mode=0;mode<3;++mode) {
            InterlockedExchange(&history::log_enabled,mode!=0);InterlockedExchange(&history::debug_enabled,mode!=1);
            CaptureBuffers buffers;Slot slot{};slot.kind=AudioEventKind::OggPage;slot.context=0x5678;slot.time=Now();slot.epoch=9;
            auto send=[&](const std::vector<uint8_t>& bytes){slot.length=unsigned(bytes.size());memcpy(slot.bytes,bytes.data(),bytes.size());ProcessOggCapture(slot,buffers);};
            if(inactive)buffers.active[slot.context]=std::make_unique<Capture>();
            for(const auto& page:prefix)send(page);
            diagnostic_lines.clear();const auto failures=failure_events;send(rejected);
            assert(buffers.active.empty()&&buffers.ready.empty()); // No invalid capture becomes publishable.
            assert(failure_events==failures+unsigned(mode!=0));
            assert(diagnostic_lines.size()==(mode==0?0u:mode==1?1u:2u));
            if(mode)assert(Contains(reason));
            if(mode==2) {
                assert(Contains("Ogg observation")&&Contains("ctx=5678 epoch=9"));
                assert(Contains("expected_seq=")&&Contains("actual_seq=")&&Contains("previous_granule="));
                assert(!Contains("spotify:track:")&&!Contains("FLOGGFY_METADATA"));
                if(std::string(reason)=="sequence-discontinuity"&&prefix.size()==1)
                    assert(Contains("expected_seq=1 actual_seq=2")&&Contains("expected_serial=42 actual_serial=42"));
                if(std::string(reason)=="sequence-discontinuity"&&prefix.size()==2)assert(Contains("expected_seq=2 actual_seq=1"));
                if(std::string(reason)=="serial-change")assert(Contains("expected_serial=42 actual_serial=43"));
                if(std::string(reason)=="invalid Ogg page rejected")assert(Contains("reason=invalid-page")&&Contains("page_known=0"));
            }
        }
    };
    const auto bos=recording_fixture::page(recording_fixture::identification(),0,2,0);
    const auto middle=recording_fixture::page({0},1,0,44100);
    exercise("sequence-discontinuity",{bos},recording_fixture::page({0},2,4,441000));
    exercise("sequence-discontinuity",{bos,middle},middle); // Backtrack/duplicate, not necessarily missing data.
    exercise("serial-change",{bos},recording_fixture::page({0},1,0,44100,43));
    exercise("unexpected-bos",{bos},recording_fixture::page({0},1,2,0));
    exercise("invalid-granule",{bos},recording_fixture::page({0},1,0,uint64_t(-2)));
    exercise("granule-regression",{bos,middle},recording_fixture::page({0},2,0,100));
    exercise("invalid-eos-granule",{bos},recording_fixture::page({0},1,4,0));
    exercise("inactive-stream",{},middle,true);
    auto corrupt=middle;corrupt.back()^=1;exercise("invalid Ogg page rejected",{bos},corrupt);
    InterlockedExchange(&history::log_enabled,1);InterlockedExchange(&history::debug_enabled,1);
}
void IncompleteAppTransitionEvidence() {
    for(unsigned variant=0;variant<3;++variant) {
        const bool same_uri=variant!=0,known_instance=variant!=2;
        CaptureBuffers buffers;Listen listen;Media current,media;std::vector<Heard> heard;
        media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
        media.playback_instance="old-instance";media.title=L"Current";media.duration=168.801;media.playing=true;
        for(unsigned i=0;i<=16;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,100+i);}
        Media next=media;next.playback_instance=known_instance?"new-instance":"";next.position=same_uri?16.5:0;
        if(!same_uri)next.spotify_uri="spotify:track:0000000000000000000002";
        diagnostic_lines.clear();const auto failures=failure_events;
        assert(!FinishAppTransition(next,listen,current,heard,buffers,116.5));
        assert(heard.empty()&&!listen.eligible&&failure_events==failures);
        assert(Contains(same_uri?"same_uri=1":"same_uri=0")&&Contains(known_instance?"instance_changed=1":"instance_changed=-1"));
        assert(Contains("last_pos=16.000/168.801")&&Contains("elapsed=0.500"));
        assert(diagnostic_lines.size()==2&&!Contains("spotify:track:")&&!Contains("old-instance")&&!Contains("new-instance"));
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
void DurableAssociationEvidence() {
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
    Connectivity();OggRejectionEvidence();IncompleteAppTransitionEvidence();AppIdentityAndSourceChanges();NaturalAppBufferingTransition();RecentPlaybackMatching();DurableAssociationEvidence();AppTailDeadline();MissingPlaybackWarning();ObservePlayback();CoverageAndFailures();HookBoundaries();TimingVerbosity();BasicModeProfiling();LoggingDisabled();
    puts("PASS: observed transitions, one FLAC failure context/outcome, original callback semantics, profiling-only intervals, basic-mode zero profiling and changed-only NLM observations");
    return 0;
}
