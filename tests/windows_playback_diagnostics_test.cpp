// Exercise real hooks/worker diagnostics without patching or controlling Spotify.
#include "../native/async_log.h"
#include "../native/client_playback.h"
namespace history {void CaptureLogEvent(LogKind,const char*,const std::string&);}
#define LogEvent CaptureLogEvent
#include "../native/audio_history.cpp"
#undef LogEvent
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include "spac_fixture.h"
#include <cassert>
#include <iostream>
#include <random>

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
size_t decode_consumed=100;
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
    encoded->data=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(encoded->data)+decode_consumed);
    encoded->size-=decode_consumed;SetLastError(1234);return result;
}
std::vector<uint8_t> PodcastMp3() {
    std::vector<uint8_t> out={'I','D','3',4,0,0,0,0,0,0};
    const auto add=[&](unsigned length,uint8_t third) {
        const auto at=out.size();out.resize(at+length);out[at]=0xff;out[at+1]=0xfb;out[at+2]=third;out[at+3]=0;
    };
    add(417,0x90);add(523,0xa2);add(417,0x90);return out;
}
void PodcastCaptureEvents() {
    const auto source=PodcastMp3();const double duration=3456.0/44100.0;
    decoded_sink_target=reinterpret_cast<void*>(0x1000);
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(19);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    Slot slot{};slot.context=0x5000;slot.time=100;slot.epoch=19;slot.capture=true;
    slot.kind=AudioEventKind::Mp3Begin;ProcessMp3Capture(slot,buffers);
    assert(buffers.active.size()==1&&buffers.active.begin()->second->mp3);
    for(size_t at=0;at<source.size();) {
        const auto amount=std::min<size_t>(97,source.size()-at);slot.kind=AudioEventKind::Mp3Bytes;
        slot.length=unsigned(amount);memcpy(slot.bytes,source.data()+at,amount);slot.time+=.01;
        ProcessMp3Capture(slot,buffers);at+=amount;
    }
    ownership::DecodedActivity activity{};activity.owner=slot.context;activity.calls=1;
    activity.produced_floats=100;activity.consumed_bytes=source.size();activity.kept_floats=100;
    activity.first=100.01;activity.last=100.2;activity.epoch=19;tracker.ObserveDecoded(activity);
    Listen listen;listen.identity="spotify:episode:0000000000000000000001\x1f" "episode-one";
    listen.eligible=true;listen.playing=true;listen.start_time=100;listen.last_time=100.3;
    listen.last_position=duration;listen.duration=duration;listen.generation=70;
    Media episode;episode.source=MediaSource::Spotify;episode.spotify_uri="spotify:episode:0000000000000000000001";
    episode.playback_instance="episode-one";episode.duration=duration;episode.playing=true;
    assert(BindReadyCapture(buffers,episode,70,100.3)&&
           "the production Ready binder must bind an active MP3 before final parsing");
    assert(FinalizeBoundMp3Capture(buffers,listen,episode,100.4));
    assert(buffers.active.empty()&&buffers.ready.size()==1&&buffers.ready[0]->mp3);
    assert(buffers.ready[0]->mp3_info.frames==3&&ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid);
    ownership::OwnershipBinding durable{};
    assert(BoundCompleteCaptureForListen(buffers,listen,episode,durable)==buffers.ready[0].get()&&
           "a fully validated exact MP3 binding survives a missing media snapshot without a timer");
    Heard heard{episode,100,100.4,70};std::vector<Heard> completed{heard};
    SchedulePublications(completed,buffers,19,100.4);SaveJob job;
    assert(saves.Pop(job)&&job.capture->mp3&&job.heard.generation==70);
    publishing_reserved.fetch_sub(job.reserved);

    ownership::DecoderOwnershipTracker direct_tracker;direct_tracker.Reset(19);
    CaptureBuffers direct;direct.ownership_tracker=&direct_tracker;
    auto direct_capture=std::make_unique<Capture>();direct_capture->mp3=true;direct_capture->born=200;
    direct_capture->last_input=200.2;direct_capture->capture_epoch=19;direct_capture->capture_context=0x6000;
    assert(direct_capture->data.Append(source.data(),source.size(),source.size()+65536));direct_capture->bytes=source.size();
    BeginCaptureOwnership(*direct_capture,direct,ownership::DecoderCodec::Mp3,0x6000,0,200,19);
    direct.active[0x6000]=std::move(direct_capture);
    activity.owner=0x6000;activity.first=200.01;activity.last=200.2;direct_tracker.ObserveDecoded(activity);
    direct_tracker.ObservePlayback("episode-two",episode.spotify_uri,71,200.3);
    auto second=episode;second.playback_instance="episode-two";
    Listen direct_listen;direct_listen.identity=second.ListenKey();direct_listen.eligible=true;direct_listen.playing=true;
    direct_listen.generation=71;direct_listen.start_time=200;direct_listen.last_time=200.3;
    direct_listen.last_position=duration;direct_listen.duration=duration;direct_listen.capture_speed=1;
    Media next=episode;next.spotify_uri="spotify:episode:0000000000000000000002";
    next.playback_instance="episode-three";next.position=0;next.duration=1;next.playing=true;
    std::vector<Heard> direct_heard;AssociateListen(next,direct_listen,second,direct_heard,direct,200.4);
    assert(direct.active.empty()&&direct.ready.size()==1&&direct_heard.size()==1&&
           direct_heard[0].generation==71&&direct_listen.generation==72&&
           "an old-to-new identity transition finalizes the completed generation's exact MP3 decoder");
    decoded_sink_target=nullptr;
}
void PodcastEndWithoutReadySuccessor() {
    const auto fragment=PodcastMp3();std::vector<uint8_t> source(fragment.begin(),fragment.begin()+10);
    for(unsigned i=0;i<1566;++i)source.insert(source.end(),fragment.begin()+10,fragment.end());
    Mp3Info info;std::string error;assert(InspectMp3Source(source.data(),source.size(),info,error));
    const auto previous_target=decoded_sink_target;decoded_sink_target=reinterpret_cast<void*>(0x1000);
    enum class EndCase { Natural, LiveTrace, BufferingTail, BufferingEarly, Early, Stale,
                         Skip, Control, PriorControl, SameInstance, MissingCapture, Ambiguous, UnrelatedLoss, MatchingLoss, GlobalLoss };
    struct End { EndCase kind; double tail,elapsed; bool completes; };
    const End cases[]={
        {EndCase::Natural,.6,.65,true},
        {EndCase::LiveTrace,1.222,1.232509,true},
        {EndCase::BufferingTail,1.6,1.65,true},
        {EndCase::BufferingEarly,1.6,1,false},
        {EndCase::Early,10,.65,false},
        {EndCase::Stale,.6,4,false},
        {EndCase::Skip,.6,.65,false},
        {EndCase::Control,.6,.65,false},
        {EndCase::PriorControl,.6,.65,false},
        {EndCase::SameInstance,.6,.65,false},
        {EndCase::MissingCapture,.6,.65,false},
        {EndCase::Ambiguous,.6,.65,false},
        {EndCase::UnrelatedLoss,.6,.65,true},
        {EndCase::MatchingLoss,.6,.65,false},
        {EndCase::GlobalLoss,.6,.65,false},
    };
    for(const auto& test:cases) {
        const auto scenario=test.kind;
        ownership::DecoderOwnershipTracker tracker;tracker.Reset(29);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        Slot slot{};slot.context=0x7100;slot.time=100;slot.epoch=29;slot.capture=true;slot.kind=AudioEventKind::Mp3Begin;
        ProcessMp3Capture(slot,buffers);
        for(size_t at=0;at<source.size();) {
            const auto amount=std::min(source.size()-at,sizeof(slot.bytes));slot.kind=AudioEventKind::Mp3Bytes;
            slot.length=unsigned(amount);memcpy(slot.bytes,source.data()+at,amount);slot.time+=.001;
            ProcessMp3Capture(slot,buffers);at+=amount;
        }
        ownership::DecodedActivity activity{};activity.owner=slot.context;activity.calls=1;
        activity.produced_floats=100;activity.consumed_bytes=source.size();activity.kept_floats=100;
        activity.first=100.01;activity.last=100.2;activity.epoch=29;tracker.ObserveDecoded(activity);
        Media episode;episode.source=MediaSource::Spotify;episode.spotify_uri="spotify:episode:0000000000000000000001";
        episode.playback_instance="episode-one";episode.duration=info.Duration();episode.playing=true;
        assert(BindReadyCapture(buffers,episode,81,100.3));
        Listen listen;listen.identity=episode.ListenKey();listen.eligible=true;listen.playing=true;
        listen.start_time=100;listen.last_time=100+episode.duration-test.tail;
        listen.last_position=episode.duration-test.tail;
        listen.duration=episode.duration;listen.generation=81;listen.capture_speed=1;
        Media buffering=episode;buffering.position=listen.last_position;buffering.control_monitoring=true;
        std::vector<Heard> heard;
        if(scenario==EndCase::UnrelatedLoss||scenario==EndCase::MatchingLoss) {
            CaptureQueueLossBatch loss;loss.count=1;
            loss.entries[0]={scenario==EndCase::MatchingLoss?slot.context:slot.context+16,29};
            assert(!ApplyCaptureQueueLosses(buffers,loss,listen.last_time));
        }
        const bool held=FinishAppTransition(buffering,listen,episode,heard,buffers,listen.last_time+.05);
        if(scenario==EndCase::MatchingLoss) {
            assert(!held&&heard.empty()&&buffers.active.empty()&&buffers.ready.empty());
            DrainReleaseAudits();continue;
        }
        assert(held);
        assert(heard.empty()&&buffers.ready.size()==1&&buffers.ownership_hold_generation==81);
        // Spotify resets playbackId and pauses in buffering after the episode ends.
        // No successor Ready arrives. Full bound audio + natural end must close it.
        if(scenario!=EndCase::SameInstance)buffering.playback_instance="episode-two";
        buffering.position=0;buffering.playing=false;
        buffering.explicit_skip=scenario==EndCase::Skip;
        buffering.explicit_control=scenario==EndCase::Control;
        buffering.prior_control=scenario==EndCase::PriorControl;
        if(scenario==EndCase::MissingCapture)buffers.ready.clear();
        if(scenario==EndCase::Ambiguous)buffers.ready.front()->ambiguous=true;
        if(scenario==EndCase::GlobalLoss) {
            CaptureQueueLossBatch loss;loss.global=true;
            assert(ApplyCaptureQueueLosses(buffers,loss,listen.last_time)&&buffers.ready.front()->ambiguous);
        }
        const double transition_time=listen.last_time+test.elapsed;
        FinishAppTransition(buffering,listen,episode,heard,buffers,transition_time);
        if(test.completes) {
            assert(heard.size()==1&&heard.front().generation==81&&
                   std::fabs(heard.front().finish-(100+episode.duration))<.001);
            assert(!buffers.pending_natural_generation&&!listen.eligible);
            SchedulePublications(heard,buffers,29,transition_time);
            SaveJob job;assert(saves.Pop(job)&&job.capture->mp3&&job.heard.generation==81&&
                               !job.heard.provisional&&heard.empty()&&buffers.ready.empty());
            publishing_reserved.fetch_sub(job.reserved);
        } else {
            assert(heard.empty()&&"prefetch, controls, stale timing or uncertain ownership cannot prove completion");
            if(scenario!=EndCase::Skip&&scenario!=EndCase::Control&&scenario!=EndCase::SameInstance)
                assert(buffers.pending_natural_generation==81);
        }
    }
    decoded_sink_target=previous_target;
    puts("PASS: natural podcast end completes without successor Ready; incomplete or uncertain transitions stay uncompleted");
}
void DirectIdentityRetriesAndIgnoresLogVerbosity() {
    constexpr char instance[]="00112233445566778899aabbccddeeff";
    playback_identity::Instance parsed{};assert(playback_identity::ParseInstance(instance,parsed));
    playback_identity_bridge={};playback_identity_metadata_generation=0;
    playback_pump_target=reinterpret_cast<void*>(0x1);
    assert(!playback_identity_bridge.Observe(0x4100,0x5100,0,1,
        parsed.bytes.data(),parsed.bytes.size(),playback_identity::SnapshotRole::PlayerPlaybackId));

    ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=std::make_unique<Capture>();capture->lossless=true;capture->decoder_owner=0x5100;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,0,1);
    buffers.active[0x5100]=std::move(capture);
    Media media;media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.playback_instance=instance;media.playing=true;
    std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
    const auto first=SetExpectedPlaybackIdentity(media,70,matches);
    assert(first==1&&!BindDirectReadyCapture(buffers,media,70,100.1,matches,first)&&
           "identity can precede the decoded-output ownership stage");
    ownership::DecodedActivity decoded{};decoded.owner=0x5100;decoded.calls=1;
    decoded.produced_floats=8192;decoded.first=100.1;decoded.last=100.2;decoded.epoch=0;
    tracker.ObserveDecoded(decoded);
    matches={};const auto retry=SetExpectedPlaybackIdentity(media,70,matches);
    assert(retry==1&&BindDirectReadyCapture(buffers,media,70,100.2,matches,retry)&&
           "the same retained identity observation binds after decoded output arrives");

    playback_identity::Instance second{};
    constexpr char second_instance[]="ffeeddccbbaa99887766554433221100";
    assert(playback_identity::ParseInstance(second_instance,second));
    auto next_capture=std::make_unique<Capture>();next_capture->lossless=true;next_capture->decoder_owner=0x5200;
    next_capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,next_capture->decoder_owner,0,101,0,2);
    buffers.active[0x5200]=std::move(next_capture);
    decoded.owner=0x5200;decoded.first=101;decoded.last=101.1;tracker.ObserveDecoded(decoded);
    assert(!playback_identity_bridge.Observe(0x4200,0x5200,0,2,
        second.bytes.data(),second.bytes.size(),playback_identity::SnapshotRole::PlayerPlaybackId));
    media.playback_instance=second_instance;media.spotify_uri="spotify:track:0000000000000000000002";
    InterlockedExchange(&history::log_level_value,1);matches={};
    const auto quiet=SetExpectedPlaybackIdentity(media,71,matches);
    assert(quiet==1&&BindDirectReadyCapture(buffers,media,71,101.2,matches,quiet)&&
           "direct identity is capture evidence, not a LogLevel 4 feature");
    InterlockedExchange(&history::log_level_value,4);
    playback_identity_bridge={};playback_identity_metadata_generation=0;playback_pump_target=nullptr;
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
    InterlockedExchange(&history::log_level_value,1);const auto off=diagnostic_lines.size();
    state.Observe(media,27.5,23);assert(diagnostic_lines.size()==off);
    InterlockedExchange(&history::log_level_value,4);
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
    assert(diagnostic_lines.size()==3&&failure_events==prior_failures+1); // Context, final audit, native failure outcome.
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
    InterlockedExchange(&history::log_level_value,1);diagnostic_lines.clear();
    slot.kind=AudioEventKind::FlacFrame;ProcessFlacCapture(slot,buffers);
    assert(diagnostic_lines.size()==1&&Contains("stream discarded")&&!Contains("incomplete cache | unknown track")&&
           "the final audit must not manufacture a second user-visible failure");
    InterlockedExchange(&history::log_level_value,4);
}
void SameContextFlacRestartBeginsFreshCapture() {
    auto complete=recording_fixture::fixture();
    uint64_t packed=0;for(unsigned i=18;i<26;++i)packed=(packed<<8)|complete[i];
    packed=(packed&~0xfffffffffULL)|32;
    for(unsigned i=0;i<8;++i)complete[18+i]=uint8_t(packed>>(56-8*i));
    std::vector<uint8_t> first(complete.begin()+42,complete.end()),second=first;
    second[4]=1;
    std::vector<uint8_t> second_header(second.begin(),second.begin()+6);
    second[6]=uint8_t(recording_fixture::crc(second_header,8,7));
    second.resize(second.size()-2);
    const auto checksum=recording_fixture::crc(second,16,0x8005);
    second.push_back(uint8_t(checksum>>8));second.push_back(uint8_t(checksum));
    complete.insert(complete.end(),second.begin(),second.end());

    CaptureBuffers buffers;Slot slot{};slot.context=0x4567;slot.epoch=7;slot.time=100;
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(42+first.size());
    memcpy(slot.bytes,complete.data(),slot.length);ProcessFlacCapture(slot,buffers);
    uint32_t frame[6]={16,44100,2,0,16,0};uint64_t number=0;
    slot.kind=AudioEventKind::FlacFrame;slot.length=32;
    memcpy(slot.bytes,frame,24);memcpy(slot.bytes+24,&number,8);
    ProcessFlacCapture(slot,buffers);
    assert(buffers.active.at(slot.context)->coverage.samples==16);

    slot.time=101;slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(first.size());
    memcpy(slot.bytes,first.data(),first.size());ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacFrame;slot.length=32;
    memcpy(slot.bytes,frame,24);memcpy(slot.bytes+24,&number,8);
    ProcessFlacCapture(slot,buffers);
    assert(buffers.active.count(slot.context)&&buffers.ready.empty()&&
           buffers.active.at(slot.context)->coverage.samples==16&&
           buffers.active.at(slot.context)->bytes==42+first.size()&&
           "a decoder seek to sample zero starts a fresh compressed lifetime");

    slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(second.size());
    memcpy(slot.bytes,second.data(),second.size());ProcessFlacCapture(slot,buffers);
    frame[5]=0;number=1;slot.kind=AudioEventKind::FlacFrame;slot.length=32;
    memcpy(slot.bytes,frame,24);memcpy(slot.bytes+24,&number,8);
    ProcessFlacCapture(slot,buffers);
    assert(buffers.active.empty()&&buffers.ready.size()==1&&
           ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid);
    auto rebuilt=buffers.ready[0]->data.Flatten();RecordingFile inspected;
    assert(rebuilt==complete&&InspectRecording(rebuilt.data(),rebuilt.size(),inspected)&&
           inspected.complete&&"the restarted lifetime passes whole-file FLAC frame/CRC inspection");

    const auto reject_restart=[&](bool corrupt,uint64_t reset_number) {
        CaptureBuffers rejected;Slot event{};event.context=0x5000+reset_number;
        event.epoch=7;event.time=200;event.kind=AudioEventKind::FlacBegin;
        ProcessFlacCapture(event,rejected);
        event.kind=AudioEventKind::FlacBytes;event.length=unsigned(42+first.size());
        memcpy(event.bytes,complete.data(),event.length);ProcessFlacCapture(event,rejected);
        frame[5]=0;number=0;event.kind=AudioEventKind::FlacFrame;event.length=32;
        memcpy(event.bytes,frame,24);memcpy(event.bytes+24,&number,8);
        ProcessFlacCapture(event,rejected);
        auto restart=first;if(corrupt)restart.back()^=1;
        event.kind=AudioEventKind::FlacBytes;event.length=unsigned(restart.size());
        memcpy(event.bytes,restart.data(),restart.size());ProcessFlacCapture(event,rejected);
        number=reset_number;event.kind=AudioEventKind::FlacFrame;event.length=32;
        memcpy(event.bytes,frame,24);memcpy(event.bytes+24,&number,8);
        ProcessFlacCapture(event,rejected);
        assert(rejected.active.empty()&&rejected.ready.empty());
    };
    reject_restart(true,0);reject_restart(false,8);
}
void CaptureOwnershipLifecycle() {
    decoder_ownership_layout={};decoder_ownership_layout.ogg_capture_to_decoder=-0x88;
    decoder_ownership_layout.flac_capture_to_decoder=0;decoder_ownership_layout_available=true;
    decoded_sink_target=reinterpret_cast<void*>(0x1000);
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
    decoder_ownership_layout={};decoder_ownership_layout_available=false;decoded_sink_target=nullptr;
}
void OwnershipDiagnosticPrivacy() {
    diagnostic_lines.clear();ownership_output_logged={};
    ownership_output_logged_count=ownership_output_logged_next=0;
    decoder_ownership_layout={};decoder_ownership_layout.flac_capture_to_decoder=0;
    decoder_ownership_layout_available=true;decoded_sink_target=reinterpret_cast<void*>(0x1000);
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
    decoder_ownership_layout={};decoder_ownership_layout_available=false;decoded_sink_target=nullptr;
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

    buffers={};send(bos_a);diagnostic_lines.clear();const auto failures=failure_events;
    send(recording_fixture::page({0},2,4,88200,41));
    assert(buffers.ogg_active.empty()&&buffers.ready.empty());
    assert(Contains("sequence-discontinuity")&&Contains("mid-stream Ogg")&&
           failure_events==failures&&
           "an unbound page-sequence gap is ignored audio, not a failed song download");

    buffers={};send(bos_a);
    buffers.ogg_active.begin()->second->bound_identity="Artist - Bound Song";
    diagnostic_lines.clear();const auto bound_failures=failure_events;
    send(recording_fixture::page({0},2,4,88200,41));
    assert(failure_events>bound_failures&&Contains("sequence-discontinuity")&&
           Contains("Bound Song")&&
           "a page-sequence gap in an identified song remains a real failure");

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
    ResetDecodedActivityQueue();
    const auto podcast=PodcastMp3();const auto podcast_context=uintptr_t(0x8888);
    QueueMp3Encoded(podcast_context,podcast.data(),2,1,7);Slot slot{};assert(!Pop(slot));
    QueueMp3Encoded(podcast_context,podcast.data()+2,podcast.size()-2,1.1,7);
    assert(Pop(slot)&&slot.kind==AudioEventKind::Mp3Begin&&slot.context==podcast_context);
    std::vector<uint8_t> captured;
    while(Pop(slot)){assert(slot.kind==AudioEventKind::Mp3Bytes);captured.insert(captured.end(),slot.bytes,slot.bytes+slot.length);}
    assert(captured==podcast&&"split ID3 signatures retain every original input byte");
    CaptureBuffers discarded;auto abandoned=std::make_unique<Capture>();abandoned->mp3=true;
    abandoned->capture_context=podcast_context;abandoned->capture_epoch=7;discarded.active[podcast_context]=std::move(abandoned);
    discarded.RecordLostActive(discarded.active[podcast_context].get(),ownership::CaptureAbortReason::Timeout);
    discarded.active.clear();
    assert(std::none_of(mp3_probes.begin(),mp3_probes.end(),[&](const auto& probe){return probe.context==podcast_context;})&&
           "all generic MP3 removal paths release decoder probe state");
    original_flac_init=Init;original_flac_read=Read;original_flac_frame=Frame;original_flac_error=Error;
    auto* context=reinterpret_cast<void*>(0x1234);auto* result=reinterpret_cast<void*>(0x5678);
    assert(FlacInitHook(context,result)==result&&init_calls==1);assert(Pop(slot)&&slot.kind==AudioEventKind::FlacBegin);
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
    decoded_fallback_reason=0;
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
    decoded_accelerator.Reset();decoded_fallback_requested=false;decoded_fallback_reason=0;
    decode_consumed=max_decoded_callback_bytes+1;
    decoded={samples,100};encoded={compressed,decode_consumed+1};
    DecodedSinkHook(context,result,&decoded,&encoded,true);
    assert(!decoded_accelerator.Enabled()&&decoded_fallback_requested.load()&&
           decoded_fallback_reason.load()==unsigned(DecodedFallbackReason::EncodedSpan)&&
           "oversized private-ABI spans fall back before any encoded-byte copy");
    decode_consumed=100;decoded_accelerator.Reset();decoded_fallback_requested=false;decoded_fallback_reason=0;
    invalid_decoded_span=true;decoded={samples,100};encoded={compressed,200};
    const auto activity_loss=decoded_activity_dropped;
    DecodedSinkHook(context,result,&decoded,&encoded,true);
    assert(decoded.data==samples+79&&decoded.size==20&&!decoded_accelerator.Enabled()&&
           decoded_fallback_requested.load()&&decoded_activity_dropped==activity_loss+1&&
           decoded_fallback_reason.load()==unsigned(DecodedFallbackReason::DecodedSpan)&&
           !PopDecodedActivity(activity)&&decode_calls==4);
    ownership::DecoderOwnershipTracker invalid_tracker;invalid_tracker.Reset(7);
    const auto invalid_capture=invalid_tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x1000,0,Now(),7);
    DrainDecodedActivity(invalid_tracker,7);
    assert(invalid_tracker.Binding(invalid_capture).state==ownership::OwnershipState::Invalidated);
    decoded_accelerator.Reset();decoded_fallback_requested=false;decoded_fallback_reason=0;
    invalid_decoded_span=false;
    InterlockedExchange(&history::capture_speed_value,1);InterlockedExchange(&history::log_level_value,1);
    decoded={samples,100};encoded={compressed,200};DecodedSinkHook(context,result,&decoded,&encoded,true);
    assert(PopDecodedActivity(activity)&&activity.produced_floats==80&&activity.kept_floats==80);
    InterlockedExchange(&history::log_level_value,4);
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
    head=tail=count=0;InterlockedExchange(&dropped,0);unsigned char burst=1;
    for(unsigned index=0;index<129;++index)
        FlacEvent(context,AudioEventKind::FlacBytes,&burst,1);
    assert(count==129&&dropped==0&&"10x transition bursts must not overflow the compressed queue");
    while(Pop(slot)) {}
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
    QueueDecodedActivity(0x1000,1,1,1,1.2,7);
    AcquireSRWLockExclusive(&queue_lock);++producer_sequence;ReleaseSRWLockExclusive(&queue_lock);
    QueueDecodedActivity(0x1000,1,1,1,1.3,7);
    assert(decoded_activity_count==2&&"a compressed lifecycle event prevents decoded evidence from spanning address reuse");
    assert(PopDecodedActivity(slot)&&slot.calls==1);
    assert(PopDecodedActivity(slot)&&slot.calls==1);
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
    diagnostic_lines.clear();InterlockedExchange(&history::log_level_value,1);
    LogTiming();assert(diagnostic_lines.empty());
    InterlockedExchange(&history::log_level_value,4);
    InterlockedExchange(&history::networking_enabled,0);InterlockedExchange(&history::hook_diagnostics_enabled,0);
}
void BasicModeProfiling() {
    InterlockedExchange(&history::log_level_value,4);
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
    InterlockedExchange(&history::log_level_value,0);
    diagnostic_lines.clear();
    {
        NativeTimer timer(native_read_timing);assert(timer.began==0);timer.Finish();
        QueueGuard guard;assert(guard.began==0&&guard.acquired==0);
    }
    assert(native_read_timing.count.load()==0&&producer_wait_timing.count.load()==0);
    PlaybackDiagnostics playback;Media media;media.title=L"Song";media.playing=true;media.playback_status=4;
    playback.Observe(media,10,1);LogTiming();
    assert(diagnostic_lines.empty()&&!playback.known);
    InterlockedExchange(&history::log_level_value,4);
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
std::unique_ptr<Capture> CompleteAacCapture(double born,double finished) {
    auto capture=std::make_unique<Capture>();capture->aac=true;capture->born=born;capture->finished=finished;
    constexpr uint32_t samples=441000,priming=100;
    auto bytes=spac_fixture::page(true,0,{spac_fixture::config(2,samples,priming)});
    const uint32_t frames=(samples+priming+1023)/1024;
    for(uint32_t base=0;base<frames;) {
        const uint32_t count=std::min<uint32_t>(frames-base,50);
        spac_fixture::append(bytes,spac_fixture::page(false,base,
            std::vector<std::vector<uint8_t>>(count,{0x21,0x10})));
        base+=count;
    }
    assert(capture->spac.Push(bytes.data(),bytes.size())==AacParse::Complete);
    assert(capture->data.Append(bytes.data(),bytes.size(),bytes.size()+65536));capture->bytes=bytes.size();
    return capture;
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
        assert(heard.size()==1&&heard[0].provisional);
        assert(buffers.active.size()==1);
        auto completed=std::move(buffers.active[1]);buffers.active.erase(1);
        completed->coverage.samples=16;completed->finished=expire?121:110;
        buffers.ready.push_back(std::move(completed));
        if(ambiguous)buffers.ready.push_back(CompleteFlacCapture(100,110));
        SchedulePublications(heard,buffers,0,expire?121:110);SaveJob job;
        assert(saves.Pop(job)==!ambiguous);
        if(!ambiguous)assert(job.heard.provisional&&ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid);
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
        if(scenario==5) {
            assert(saves.Pop(rejected_job)&&pending.empty()&&
                   "an ordered natural lifecycle remains valid regardless of elapsed wall time");
            publishing_reserved.fetch_sub(rejected_job.reserved);
        } else assert(pending.empty()&&!saves.Pop(rejected_job)&&
                      "seek, pause, unknown or manual transition cannot recover a listen");
    }
}
void EmptyPlaybackIdNaturalTransitionIsRetried() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.playback_instance="first";media.title=L"Previous";media.duration=16;media.playing=true;
    for(unsigned i=0;i<=4;++i){media.position=i;AssociateListen(media,listen,current,heard,buffers,100+i);}
    auto active=CompleteFlacCapture(100,0);active->coverage.samples=8;active->finished=0;
    buffers.active[1]=std::move(active);

    Media buffering;buffering.source=MediaSource::Spotify;buffering.transition_pending=true;
    buffering.control_monitoring=true;
    auto controlled_listen=listen;CaptureBuffers controlled_buffers;
    auto controlled=buffering;controlled.explicit_control=true;
    assert(!FinishAppTransition(controlled,controlled_listen,current,heard,controlled_buffers,105)&&
           !controlled_buffers.pending_natural_generation&&!controlled_listen.eligible&&
           "an explicit control cannot become a pending natural transition");
    assert(FinishAppTransition(buffering,listen,current,heard,buffers,105)&&heard.empty()&&
           buffers.pending_natural_generation==listen.generation&&listen.eligible&&
           "a natural buffering snapshot without the next playback ID stays pending");

    auto gap_listen=listen;CaptureBuffers gap_buffers;
    gap_buffers.pending_natural_generation=gap_listen.generation;
    gap_buffers.pending_natural_time=105;gap_buffers.pending_natural_listen=gap_listen;
    auto controlled_ready=media;controlled_ready.playback_instance="second";
    controlled_ready.spotify_uri="spotify:track:0000000000000000000002";
    controlled_ready.explicit_control=true;
    std::vector<Heard> gap_heard;
    assert(!FinishPendingNaturalTransition(controlled_ready,gap_listen,current,gap_heard,gap_buffers)&&
           !gap_buffers.pending_natural_generation&&!gap_listen.eligible&&gap_heard.empty()&&
           "a control during the buffering gap cancels pending natural recovery");

    auto prior_listen=listen;CaptureBuffers prior_buffers;
    prior_buffers.pending_natural_generation=prior_listen.generation;
    prior_buffers.pending_natural_time=105;prior_buffers.pending_natural_listen=prior_listen;
    auto prior_ready=controlled_ready;prior_ready.explicit_control=false;prior_ready.prior_control=true;
    std::vector<Heard> prior_heard;
    assert(!FinishPendingNaturalTransition(prior_ready,prior_listen,current,prior_heard,prior_buffers)&&
           !prior_buffers.pending_natural_generation&&!prior_listen.eligible&&prior_heard.empty()&&
           "a control acknowledged by the successor must cancel pending natural recovery");

    auto same=media;same.position=4.2;same.control_monitoring=true;
    assert(!FinishPendingNaturalTransition(same,listen,current,heard,buffers)&&
           buffers.pending_natural_generation==listen.generation&&
           "an intermediate Ready snapshot with the old ID keeps waiting");
    AssociateListen(same,listen,current,heard,buffers,105.2);

    auto seek_listen=listen;auto seek_current=current;CaptureBuffers seek_buffers;
    seek_buffers.pending_natural_generation=seek_listen.generation;
    auto seek=same;seek.position=13;
    assert(!FinishPendingNaturalTransition(seek,seek_listen,seek_current,heard,seek_buffers));
    AssociateListen(seek,seek_listen,seek_current,heard,seek_buffers,105.3);

    Media next;next.source=MediaSource::Spotify;
    next.spotify_uri="spotify:track:0000000000000000000002";
    next.playback_instance="second";next.title=L"Next";next.duration=20;next.position=.2;
    next.playing=true;next.control_monitoring=true;
    assert(FinishPendingNaturalTransition(next,listen,current,heard,buffers)&&
           heard.size()==1&&heard[0].provisional&&buffers.active.count(1)&&
           "the first real next-track snapshot retries final capture reconciliation");
    assert(!FinishPendingNaturalTransition(next,seek_listen,seek_current,heard,seek_buffers)&&
           !seek_buffers.pending_natural_generation&&
           "a same-ID seek invalidates the pending natural transition");

    CaptureBuffers prior_blank_buffers;std::vector<Heard> prior_blank_heard;
    Listen prior_blank_listen;Media prior_blank_current,prior_blank=media;
    prior_blank.playback_instance.clear();
    for(unsigned i=0;i<=4;++i) {
        prior_blank.position=i;
        AssociateListen(prior_blank,prior_blank_listen,prior_blank_current,
                        prior_blank_heard,prior_blank_buffers,200+i);
    }
    Media known_buffering=buffering;
    known_buffering.playback_instance="successor-stage";
    assert(FinishAppTransition(known_buffering,prior_blank_listen,prior_blank_current,
                               prior_blank_heard,prior_blank_buffers,205)&&
           prior_blank_buffers.pending_natural_generation==prior_blank_listen.generation&&
           "a missing Ready ID remains pending when buffering supplies its stage ID");
    Media skipped_stage=next;skipped_stage.playback_instance="later-stage";
    assert(!FinishPendingNaturalTransition(skipped_stage,prior_blank_listen,
                                           prior_blank_current,prior_blank_heard,
                                           prior_blank_buffers)&&
           !prior_blank_buffers.pending_natural_generation&&
           !prior_blank_listen.eligible&&prior_blank_heard.empty()&&
           "a later Ready ID cannot consume evidence for a skipped buffering stage");
}
void UnclassifiedMonitoredTransitionWaitsForReady() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current;
    current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="outgoing";current.title=L"Outgoing";
    current.duration=301.92;current.playing=true;
    for(unsigned second=0;second<=301;++second) {
        current.position=second;
        AssociateListen(current,listen,current,heard,buffers,100+second);
    }
    current.position=current.duration;
    AssociateListen(current,listen,current,heard,buffers,401.92);
    assert(listen.eligible&&listen.playing&&heard.empty());
    // The immediately preceding same-instance buffering stage proved that the
    // outgoing decoder still owned this listen before Spotify exposed a new ID.
    buffers.ownership_hold_generation=listen.generation;

    Media transition;transition.source=MediaSource::Spotify;
    transition.spotify_uri="spotify:track:0000000000000000000002";
    transition.playback_instance="successor";transition.control_monitoring=true;
    transition.playing=false;
    assert(FinishAppTransition(transition,listen,current,heard,buffers,402.1)&&
           buffers.pending_natural_generation==listen.generation&&listen.eligible&&
           heard.empty()&&
           "an uncontrolled monitored transition waits for its Ready identity instead of guessing");

    Media ready=transition;ready.playing=true;ready.position=.1;ready.duration=294.507;
    ready.title=L"Successor";
    assert(FinishPendingNaturalTransition(ready,listen,current,heard,buffers)&&
           heard.size()==1&&!buffers.pending_natural_generation&&
           "the successor Ready stage supplies the positive evidence that completes the transition");
}
void BoundNaturalTransitionUsesFinalCaptureEvidence() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(24);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=CompleteFlacCapture(100,0);capture->coverage.samples=8;capture->finished=0;
    capture->decoder_owner=0x7000;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,24);
    ownership::DecodedActivity activity{};activity.owner=capture->decoder_owner;
    activity.calls=1;activity.produced_floats=4096;activity.first=100;activity.last=100.1;
    activity.epoch=24;tracker.ObserveDecoded(activity);
    tracker.ObservePlayback("first","spotify:track:0000000000000000000001",90,100.2);
    assert(tracker.Binding(capture->capture_token).state==ownership::OwnershipState::Bound);
    buffers.active[1]=std::move(capture);

    Media current;current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="first";current.title=L"Crossfade";current.duration=16;
    current.playing=true;
    Listen listen;listen.identity=current.ListenKey();listen.eligible=true;listen.playing=true;
    listen.start_time=100;listen.last_time=100.2;listen.last_position=0;
    listen.duration=16;listen.generation=90;listen.capture_speed=1;
    std::vector<Heard> heard;
    Media buffering;buffering.source=MediaSource::Spotify;buffering.transition_pending=true;
    buffering.control_monitoring=true;
    assert(FinishAppTransition(buffering,listen,current,heard,buffers,100.8));
    Media next;next.source=MediaSource::Spotify;
    next.spotify_uri="spotify:track:0000000000000000000002";
    next.playback_instance="second";next.playing=true;next.control_monitoring=true;
    assert(FinishPendingNaturalTransition(next,listen,current,heard,buffers)&&heard.size()==1&&
           heard[0].provisional&&
           "an exact bound decoder can await final validation beyond the generic timing estimate");

    auto complete=std::move(buffers.active[1]);buffers.active.erase(1);
    complete->coverage.samples=16;complete->finished=102;
    tracker.CompleteCapture(complete->capture_token,102);buffers.ready.push_back(std::move(complete));
    SchedulePublications(heard,buffers,24,102);SaveJob job;
    assert(saves.Pop(job)&&ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid&&
           "only a complete, valid bound capture is published after the natural transition");
    publishing_reserved.fetch_sub(job.reserved);
}
void DjVoiceDoesNotBlockMusicStart() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(25);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto voice=std::make_unique<Capture>();voice->mp3=true;voice->born=100;
    voice->decoder_owner=0x8100;voice->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,voice->decoder_owner,0,100,25);
    const auto voice_token=voice->capture_token;buffers.active[0x8100]=std::move(voice);
    ownership::DecodedActivity voice_output{};voice_output.owner=0x8100;voice_output.calls=1;
    voice_output.produced_floats=2304;voice_output.first=100;voice_output.last=100.1;
    voice_output.epoch=25;tracker.ObserveDecoded(voice_output);

    auto music_capture=CompleteFlacCapture(100.02,0);music_capture->coverage.samples=8;
    music_capture->finished=0;music_capture->decoder_owner=0x8200;
    music_capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,music_capture->decoder_owner,0,100.02,25);
    const auto music_token=music_capture->capture_token;
    buffers.active[0x8200]=std::move(music_capture);
    auto music_output=voice_output;music_output.owner=0x8200;music_output.produced_floats=8192;
    music_output.first=100.02;music_output.last=100.12;tracker.ObserveDecoded(music_output);
    tracker.ObservePlayback("music","spotify:track:0000000000000000000002",0,105);
    assert(tracker.Binding(voice_token).state==ownership::OwnershipState::AwaitingPlaybackInstance);
    assert(tracker.Binding(music_token).state==ownership::OwnershipState::Bound);
    buffers.confirmed_transition_playback=tracker.PlaybackToken("music");

    Media current;current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="old";current.duration=200;current.playing=true;
    Listen listen;listen.identity=current.ListenKey();listen.generation=1;listen.eligible=false;
    Media music;music.source=MediaSource::Spotify;
    music.spotify_uri="spotify:track:0000000000000000000002";
    music.playback_instance="music";music.title=L"Music after DJ";music.duration=16;
    music.position=3.4;music.playing=true;
    std::vector<Heard> heard;
    AssociateListen(music,listen,current,heard,buffers,105);
    assert(listen.eligible&&listen.generation==2&&std::fabs(listen.start_time-101.6)<.001&&
           "a uniquely bound music decoder proves the start hidden by the DJ metadata gap");
    assert(buffers.active.size()==1&&buffers.active.count(0x8200)==1&&
           tracker.Binding(voice_token).state==ownership::OwnershipState::Invalidated&&
           "unbound DJ MP3 is discarded without competing with the music capture");
    tracker.ObservePlayback("music",music.spotify_uri,listen.generation,105);
    assert(tracker.Binding(music_token).listen_generation==listen.generation);

    ownership::DecoderOwnershipTracker late_tracker;late_tracker.Reset(26);
    CaptureBuffers late;late.ownership_tracker=&late_tracker;
    auto late_voice=std::make_unique<Capture>();late_voice->mp3=true;late_voice->born=200;
    late_voice->decoder_owner=0x8250;late_voice->capture_token=late_tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,late_voice->decoder_owner,0,200,26);
    late.active[0x8250]=std::move(late_voice);auto late_voice_output=music_output;
    late_voice_output.owner=0x8250;late_voice_output.first=200;late_voice_output.last=200.05;
    late_voice_output.epoch=26;late_tracker.ObserveDecoded(late_voice_output);
    auto mid=CompleteFlacCapture(200,0);mid->coverage.samples=8;mid->finished=0;
    mid->decoder_owner=0x8300;mid->capture_token=late_tracker.BeginCapture(
        ownership::DecoderCodec::Flac,mid->decoder_owner,0,200,26);
    late.active[0x8300]=std::move(mid);auto mid_output=music_output;
    mid_output.owner=0x8300;mid_output.first=200;mid_output.last=200.1;mid_output.epoch=26;
    late_tracker.ObserveDecoded(mid_output);
    late_tracker.ObservePlayback("mid","spotify:track:0000000000000000000003",0,201);
    Listen mid_listen;mid_listen.identity=current.ListenKey();mid_listen.generation=1;
    Media mid_media=music;mid_media.spotify_uri="spotify:track:0000000000000000000003";
    mid_media.playback_instance="mid";mid_media.position=10;
    AssociateListen(mid_media,mid_listen,current,heard,late,201);
    assert(!mid_listen.eligible&&late.active.size()==1&&late.active.count(0x8300)&&
           "an MP3 exclusion without a confirmed playback transition cannot manufacture prior coverage");

    ownership::DecoderOwnershipTracker ogg_tracker;ogg_tracker.Reset(27);
    CaptureBuffers ogg_buffers;ogg_buffers.ownership_tracker=&ogg_tracker;
    auto ogg_voice=std::make_unique<Capture>();ogg_voice->mp3=true;ogg_voice->born=300;
    ogg_voice->decoder_owner=0x8350;ogg_voice->capture_token=ogg_tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,ogg_voice->decoder_owner,0,300,27);
    ogg_buffers.active[0x8350]=std::move(ogg_voice);
    auto ogg_voice_output=music_output;ogg_voice_output.owner=0x8350;
    ogg_voice_output.first=300;ogg_voice_output.last=300.05;ogg_voice_output.epoch=27;
    ogg_tracker.ObserveDecoded(ogg_voice_output);
    auto ogg=std::make_unique<Capture>();ogg->born=300;ogg->decoder_owner=0x8400;
    ogg->stream.active=true;ogg->stream.rate=44100;ogg->stream.channels=2;ogg->stream.pages=1;
    ogg->capture_token=ogg_tracker.BeginCapture(
        ownership::DecoderCodec::Ogg,ogg->decoder_owner,42,300,27);
    ogg_buffers.ogg_active[{0x8400,42}]=std::move(ogg);
    auto ogg_output=music_output;ogg_output.owner=0x8400;ogg_output.first=300;
    ogg_output.last=300.1;ogg_output.epoch=27;ogg_tracker.ObserveDecoded(ogg_output);
    ogg_tracker.ObservePlayback("ogg-music","spotify:track:0000000000000000000004",0,305);
    ogg_buffers.confirmed_transition_playback=ogg_tracker.PlaybackToken("ogg-music");
    Listen ogg_listen;ogg_listen.identity=current.ListenKey();ogg_listen.generation=1;
    Media ogg_media=music;ogg_media.spotify_uri="spotify:track:0000000000000000000004";
    ogg_media.playback_instance="ogg-music";ogg_media.position=3.4;
    AssociateListen(ogg_media,ogg_listen,current,heard,ogg_buffers,305);
    assert(ogg_listen.eligible&&
           "an active bound Ogg stream can recover a start before its duration is known");

    CaptureBuffers prior_buffers;Listen prior_listen;std::vector<Heard> prior_heard;
    Media prior_media=current;prior_media.spotify_uri="spotify:track:0000000000000000000005";
    prior_media.playback_instance="prior";prior_media.duration=60;prior_media.position=59.8;
    prior_listen.identity=prior_media.ListenKey();prior_listen.eligible=true;prior_listen.playing=true;
    prior_listen.duration=60;prior_listen.last_position=59.8;prior_listen.last_time=400;
    prior_listen.start_time=340;prior_listen.generation=1;
    Media successor=music;successor.spotify_uri="spotify:track:0000000000000000000006";
    successor.playback_instance="successor";successor.position=0;successor.duration=60;
    successor.prior_control=true;
    AssociateListen(successor,prior_listen,prior_media,prior_heard,prior_buffers,400.1);
    assert(prior_heard.empty()&&prior_listen.eligible&&prior_listen.generation==2&&
           "a successor Ready rejects the controlled prior listen but starts its own coverage");
}
void CurrentItemChangeReconcilesDjSuccessor() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(28);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    ownership::DecodedActivity output{};output.calls=1;output.produced_floats=8192;
    output.epoch=28;

    const auto outgoing=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x8500,0,100,28,1);
    output.owner=0x8500;output.first=100;output.last=100.1;output.sequence=2;
    tracker.ObserveDecoded(output);
    tracker.ObservePlayback("song-a","spotify:track:0000000000000000000001",1,100.2);
    assert(tracker.Binding(outgoing).state==ownership::OwnershipState::Bound);

    auto voice=std::make_unique<Capture>();voice->mp3=true;voice->born=101;
    voice->decoder_owner=0x8600;voice->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,voice->decoder_owner,0,101,28,3);
    const auto voice_token=voice->capture_token;buffers.active[0x8600]=std::move(voice);
    output.owner=0x8600;output.first=101;output.last=101.1;output.sequence=4;
    tracker.ObserveDecoded(output);

    auto music=CompleteFlacCapture(101.1,0);music->finished=0;music->coverage.samples=8;
    music->flac.rate=1;music->flac.total_samples=40;music->decoder_owner=0x8700;
    music->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,music->decoder_owner,0,101.1,28,5);
    const auto music_token=music->capture_token;buffers.active[0x8700]=std::move(music);
    output.owner=0x8700;output.first=101.1;output.last=101.2;output.sequence=6;
    tracker.ObserveDecoded(output);
    assert(tracker.Binding(voice_token).state==ownership::OwnershipState::Conflict&&
           tracker.Binding(music_token).state==ownership::OwnershipState::Conflict&&
           "the production ordering must reproduce the interstitial ambiguity before recovery");
    buffers.unidentified_interstitial_active=true;

    Media current;current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="song-a";current.title=L"Song A";current.duration=60;
    current.position=5;current.playing=true;
    Listen listen;listen.identity=current.ListenKey();listen.generation=1;listen.eligible=true;
    listen.playing=true;listen.start_time=96;listen.last_time=101;listen.last_position=5;
    listen.duration=60;listen.capture_speed=1;
    Media successor;successor.source=MediaSource::Spotify;
    successor.spotify_uri="spotify:track:0000000000000000000002";
    successor.playback_instance="song-b";successor.title=L"Song B";successor.artist=L"Artist";
    successor.album=L"Album";successor.duration=40;successor.position=3.2;successor.playing=true;
    std::vector<Heard> heard;
    const bool item_changed=SpotifyCurrentItemChanged(current,successor);
    AssociateListen(successor,listen,current,heard,buffers,102);
    const auto playback=BindReadyCapture(buffers,current,listen.generation,102,0,{},
        item_changed&&buffers.unidentified_interstitial_active);
    if(playback)ClearUnidentifiedSuccessor(buffers);
    assert(playback&&item_changed&&listen.eligible&&
           tracker.Binding(music_token).state==ownership::OwnershipState::Bound&&
           tracker.Binding(music_token).listen_generation==listen.generation&&
           tracker.Binding(voice_token).state==ownership::OwnershipState::Invalidated&&
           !buffers.unidentified_interstitial_active&&
           "a verified current-item change must reconsider the unique music successor");

    for(unsigned second=1;second<=31;++second) {
        successor.position=3.2+second;AssociateListen(successor,listen,current,heard,buffers,102+second);
        assert(BindReadyCapture(buffers,current,listen.generation,102+second,0,{},false)==playback&&
               tracker.Binding(music_token).state==ownership::OwnershipState::Bound);
    }

    ownership::DecoderOwnershipTracker unchanged_tracker;unchanged_tracker.Reset(29);
    CaptureBuffers unchanged;unchanged.ownership_tracker=&unchanged_tracker;
    const auto old=unchanged_tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x8800,0,200,29,1);
    output.epoch=29;output.owner=0x8800;output.first=200;output.last=200.1;output.sequence=2;
    unchanged_tracker.ObserveDecoded(output);
    unchanged_tracker.ObservePlayback("same","spotify:track:0000000000000000000003",1,200.2);
    assert(unchanged_tracker.Binding(old).state==ownership::OwnershipState::Bound);
    auto unknown_voice=std::make_unique<Capture>();unknown_voice->mp3=true;
    unknown_voice->decoder_owner=0x8900;unknown_voice->capture_token=unchanged_tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,0x8900,0,201,29,3);
    const auto unknown_voice_token=unknown_voice->capture_token;
    unchanged.active[0x8900]=std::move(unknown_voice);
    output.owner=0x8900;output.first=201;output.last=201.1;output.sequence=4;
    unchanged_tracker.ObserveDecoded(output);
    auto unresolved=CompleteFlacCapture(201.1,0);unresolved->finished=0;unresolved->coverage.samples=8;
    unresolved->flac.rate=1;unresolved->flac.total_samples=40;unresolved->decoder_owner=0x8a00;
    unresolved->capture_token=unchanged_tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x8a00,0,201.1,29,5);
    const auto unresolved_token=unresolved->capture_token;unchanged.active[0x8a00]=std::move(unresolved);
    output.owner=0x8a00;output.first=201.1;output.last=201.2;output.sequence=6;
    unchanged_tracker.ObserveDecoded(output);
    assert(unchanged_tracker.Binding(unresolved_token).state==ownership::OwnershipState::Conflict);
    Media unchanged_media=successor;unchanged_media.spotify_uri="spotify:track:0000000000000000000003";
    unchanged_media.playback_instance="same";unchanged_media.position=31;
    assert(ExcludeUnboundMp3Interludes(unchanged,unchanged_media)&&
           unchanged_tracker.Binding(unknown_voice_token).state==ownership::OwnershipState::Invalidated);
    assert(!BindReadyCapture(unchanged,unchanged_media,2,232,0,{},false)&&
           unchanged_tracker.Binding(unresolved_token).state==ownership::OwnershipState::Conflict&&
           "thirty seconds without a current-item change cannot reopen a conflict");
    auto paused=unchanged_media;paused.playing=false;
    assert(!BindReadyCapture(unchanged,paused,2,232,0,{},true));
    auto unidentified=unchanged_media;unidentified.spotify_uri.clear();
    assert(!BindReadyCapture(unchanged,unidentified,2,232,0,{},true));
}
void CurrentItemChangeBeforeStreamOutputReconcilesDjSuccessor() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(30);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    ownership::DecodedActivity output{};output.calls=1;output.produced_floats=8192;
    output.epoch=30;
    const auto outgoing=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,0x9100,0,300,30,1);
    output.owner=0x9100;output.first=300;output.last=300.1;output.sequence=2;
    tracker.ObserveDecoded(output);
    tracker.ObservePlayback("before-a","spotify:track:0000000000000000000001",1,300.2);
    assert(tracker.Binding(outgoing).state==ownership::OwnershipState::Bound);

    Media current;current.source=MediaSource::Spotify;
    current.spotify_uri="spotify:track:0000000000000000000001";
    current.playback_instance="before-a";current.title=L"Before A";current.duration=60;
    current.position=5;current.playing=true;
    Listen listen;listen.identity=current.ListenKey();listen.generation=1;listen.eligible=true;
    listen.playing=true;listen.start_time=295;listen.last_time=300;listen.last_position=5;
    listen.duration=60;listen.capture_speed=1;
    Media successor;successor.source=MediaSource::Spotify;
    successor.spotify_uri="spotify:track:0000000000000000000002";
    successor.playback_instance="before-b";successor.title=L"Before B";
    successor.artist=L"Artist";successor.album=L"Album";successor.duration=40;
    successor.position=3.2;successor.playing=true;
    buffers.unidentified_interstitial_active=true;
    assert(PrepareUnidentifiedSuccessor(buffers,current,successor)&&
           "the item-change stage must survive until its decoder output arrives");
    std::vector<Heard> heard;
    AssociateListen(successor,listen,current,heard,buffers,302);
    assert(!BindReadyCapture(buffers,current,listen.generation,302,0,{},true));

    auto voice=std::make_unique<Capture>();voice->mp3=true;voice->decoder_owner=0x9200;
    voice->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Mp3,voice->decoder_owner,0,302.1,30,3);
    const auto voice_token=voice->capture_token;buffers.active[voice->decoder_owner]=std::move(voice);
    output.owner=0x9200;output.first=302.1;output.last=302.2;output.sequence=4;
    tracker.ObserveDecoded(output);
    auto music=CompleteFlacCapture(302.2,0);music->finished=0;music->coverage.samples=8;
    music->flac.rate=1;music->flac.total_samples=40;music->decoder_owner=0x9300;
    music->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,music->decoder_owner,0,302.2,30,5);
    const auto music_token=music->capture_token;buffers.active[music->decoder_owner]=std::move(music);
    output.owner=0x9300;output.first=302.2;output.last=302.3;output.sequence=6;
    tracker.ObserveDecoded(output);
    assert(tracker.Binding(voice_token).state==ownership::OwnershipState::Conflict&&
           tracker.Binding(music_token).state==ownership::OwnershipState::Conflict);

    successor.position=3.7;
    const bool reconcile=PrepareUnidentifiedSuccessor(buffers,current,successor);
    AssociateListen(successor,listen,current,heard,buffers,302.5);
    const auto playback=BindReadyCapture(
        buffers,current,listen.generation,302.5,0,{},reconcile);
    if(playback)ClearUnidentifiedSuccessor(buffers);
    assert(playback&&listen.eligible&&
           tracker.Binding(music_token).state==ownership::OwnershipState::Bound&&
           tracker.Binding(voice_token).state==ownership::OwnershipState::Invalidated&&
           !buffers.unidentified_interstitial_active&&
           "metadata-before-output and output-before-metadata must converge identically");
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
void DecoderOwnershipFinalReconciliation() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(7);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=CompleteFlacCapture(100,101.5);capture->decoder_owner=0x1000;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,7);
    ownership::DecodedActivity decoded{};decoded.owner=capture->decoder_owner;
    decoded.calls=1;decoded.produced_floats=8192;decoded.consumed_bytes=16384;
    decoded.kept_floats=818;decoded.first=100;decoded.last=101.4;decoded.epoch=7;
    tracker.ObserveDecoded(decoded);
    tracker.ObservePlayback("same-song-play-1","spotify:track:same",2,100.1);
    tracker.CompleteCapture(capture->capture_token,101.5);
    assert(tracker.Binding(capture->capture_token).state==ownership::OwnershipState::Bound);
    buffers.ready.push_back(std::move(capture));

    Listen listen;listen.eligible=true;
    listen.playing=true;listen.start_time=100;listen.last_time=101.5;
    listen.last_position=1.5;listen.duration=16;listen.generation=2;listen.capture_speed=10;
    Media current;current.source=MediaSource::Spotify;current.spotify_uri="spotify:track:same";
    current.playback_instance="same-song-play-1";current.duration=16;current.playing=true;
    listen.identity=current.ListenKey();
    std::vector<Heard> heard;

    // Buffering repeats the old instance before the new playbackId is known.
    // A complete capture already bound to this listen must survive that gap.
    Media buffering=current;buffering.playing=false;buffering.control_monitoring=true;
    auto seek=buffering;seek.position=13;auto seek_listen=listen;
    std::vector<Heard> seek_heard;
    tracker.ObserveTransition(seek.playback_instance,seek.spotify_uri,101.55,false,false);
    assert(!FinishAppTransition(seek,seek_listen,current,seek_heard,buffers,101.55)&&
           !seek_listen.eligible&&seek_heard.empty()&&
           "same-instance forward seeks cannot use the ownership cache hold");
    auto backward=buffering;backward.position=0;auto backward_listen=listen;
    backward_listen.last_position=4;std::vector<Heard> backward_heard;
    tracker.ObserveTransition(backward.playback_instance,backward.spotify_uri,101.56,false,false);
    assert(!FinishAppTransition(backward,backward_listen,current,backward_heard,buffers,101.56)&&
           !backward_listen.eligible&&backward_heard.empty()&&
           "same-instance backward seeks cannot use the ownership cache hold");
    auto controlled=buffering;controlled.position=listen.last_position;controlled.explicit_control=true;
    auto controlled_listen=listen;std::vector<Heard> controlled_heard;
    tracker.ObserveTransition(controlled.playback_instance,controlled.spotify_uri,101.57,true,false);
    assert(!FinishAppTransition(controlled,controlled_listen,current,controlled_heard,buffers,101.57)&&
           !controlled_listen.eligible&&controlled_heard.empty()&&
           "observed small seeks cannot use the ownership cache hold");
    auto unmonitored=buffering;unmonitored.control_monitoring=false;
    auto unmonitored_listen=listen;std::vector<Heard> unmonitored_heard;
    tracker.ObserveTransition(unmonitored.playback_instance,unmonitored.spotify_uri,101.575,false,false);
    assert(!FinishAppTransition(unmonitored,unmonitored_listen,current,unmonitored_heard,buffers,101.575)&&
           !unmonitored_listen.eligible&&unmonitored_heard.empty()&&
           "missing action monitoring disables non-natural ownership recovery");
    auto direct=current;direct.playback_instance="same-song-play-2";direct.playing=false;
    direct.control_monitoring=true;
    auto direct_listen=listen;std::vector<Heard> direct_heard;
    tracker.ObserveTransition(direct.playback_instance,direct.spotify_uri,101.58,false,false);
    assert(!FinishAppTransition(direct,direct_listen,current,direct_heard,buffers,101.58)&&
           direct_heard.empty()&&"a changed playbackId without a prior continuity hold cannot recover");
    tracker.ObserveTransition(buffering.playback_instance,buffering.spotify_uri,101.6,false,false);
    assert(FinishAppTransition(buffering,listen,current,heard,buffers,101.6));
    assert(listen.eligible&&heard.empty()&&buffers.ready.size()==1);

    // The same URI starts again with a distinct playbackId. Ownership, rather
    // than title/URI/timing, closes the first listen and assigns its metadata.
    Media replay=current;replay.playback_instance="same-song-play-2";
    replay.playing=false;replay.natural_transition=false;
    replay.control_monitoring=true;
    tracker.ObserveTransition(replay.playback_instance,replay.spotify_uri,101.8,false,false);
    assert(FinishAppTransition(replay,listen,current,heard,buffers,101.8)&&heard.empty()&&
           "an unclassified successor stage is not treated as a completed listen");
    auto replay_ready=replay;replay_ready.playing=true;replay_ready.position=.1;
    assert(FinishPendingNaturalTransition(replay_ready,listen,current,heard,buffers));
    assert(heard.size()==1&&heard[0].generation==2&&heard[0].provisional);
    SchedulePublications(heard,buffers,7,101.8);SaveJob job;
    assert(saves.Pop(job)&&job.capture->capture_token&&job.heard.generation==2&&
           "bound decoder ownership is the final metadata reconciliation path");

    // Two complete files overlap the legacy timing window. Exact ownership
    // generations must select the first play instead of declaring ambiguity.
    ownership::DecoderOwnershipTracker exact;exact.Reset(8);
    CaptureBuffers alternatives;alternatives.ownership_tracker=&exact;
    const auto add=[&](uintptr_t owner,const char* playback,uint64_t generation,
                       double begun,double output,double observed) {
        auto item=CompleteFlacCapture(begun,101.5);item->decoder_owner=owner;
        item->capture_token=exact.BeginCapture(ownership::DecoderCodec::Flac,owner,0,begun,8);
        ownership::DecodedActivity activity{};activity.owner=owner;activity.calls=1;
        activity.produced_floats=8192;activity.consumed_bytes=16384;activity.kept_floats=818;
        activity.first=activity.last=output;activity.epoch=8;exact.ObserveDecoded(activity);
        exact.ObservePlayback(playback,"spotify:track:same",generation,observed);
        exact.CompleteCapture(item->capture_token,101.5);
        assert(exact.Binding(item->capture_token).state==ownership::OwnershipState::Bound);
        alternatives.ready.push_back(std::move(item));
    };
    add(0x2000,"play-one",10,100,100,100.1);
    add(0x3000,"play-two",11,100.5,100.5,100.6);
    Heard first;first.media.source=MediaSource::Spotify;first.media.playback_instance="play-one";
    first.media.duration=16;first.start=100;first.finish=101.8;first.generation=10;
    std::vector<Heard> exact_heard{first};
    SchedulePublications(exact_heard,alternatives,8,101.8);
    assert(saves.Pop(job)&&job.heard.generation==10&&alternatives.ready.size()==1&&
           "exact playback ownership must defeat overlapping timing candidates");

    ownership::DecoderOwnershipTracker partial;partial.Reset(9);
    CaptureBuffers incomplete;incomplete.ownership_tracker=&partial;
    auto short_output=CompleteFlacCapture(200,201.5);short_output->decoder_owner=0x4000;
    short_output->coverage.samples=15; // Complete bytes, but not contiguous decoded-frame coverage.
    short_output->capture_token=partial.BeginCapture(
        ownership::DecoderCodec::Flac,short_output->decoder_owner,0,200,9);
    ownership::DecodedActivity little{};little.owner=short_output->decoder_owner;
    little.calls=1;little.produced_floats=16;little.consumed_bytes=16;little.kept_floats=2;
    little.first=200;little.last=201.4;little.epoch=9;partial.ObserveDecoded(little);
    partial.ObservePlayback("partial-one","spotify:track:same",20,200.1);
    partial.CompleteCapture(short_output->capture_token,201.5);
    incomplete.ready.push_back(std::move(short_output));
    Listen partial_listen;partial_listen.identity="spotify:track:same\x1fpartial-one";
    partial_listen.eligible=true;partial_listen.playing=true;partial_listen.start_time=200;
    partial_listen.last_time=201.5;partial_listen.last_position=1.5;partial_listen.duration=16;
    partial_listen.generation=20;partial_listen.capture_speed=10;
    Media partial_current;partial_current.source=MediaSource::Spotify;
    partial_current.spotify_uri="spotify:track:same";partial_current.playback_instance="partial-one";
    partial_current.duration=16;partial_current.playing=true;
    Media partial_next=partial_current;partial_next.playback_instance="partial-two";
    partial_next.playing=false;partial.ObserveTransition(
        partial_next.playback_instance,partial_next.spotify_uri,201.8,false,false);
    std::vector<Heard> partial_heard;
    assert(!FinishAppTransition(partial_next,partial_listen,partial_current,partial_heard,incomplete,201.8)&&
           partial_heard.empty()&&"ownership without complete per-capture FLAC coverage cannot recover a listen");

    ownership::DecoderOwnershipTracker ogg_tracker;ogg_tracker.Reset(10);
    CaptureBuffers ogg_buffers;ogg_buffers.ownership_tracker=&ogg_tracker;
    auto ogg=std::make_unique<Capture>();ogg->born=300;ogg->finished=300.5;ogg->decoder_owner=0x5000;
    const auto ogg_bytes=recording_fixture::ogg();
    assert(ogg->data.Append(ogg_bytes.data(),ogg_bytes.size(),ogg_bytes.size()+65536));
    ogg->bytes=ogg_bytes.size();ogg->stream.complete=true;ogg->stream.serial=42;
    ogg->stream.rate=44100;ogg->stream.channels=2;ogg->stream.pages=9;ogg->stream.samples=44100;
    ogg->capture_token=ogg_tracker.BeginCapture(ownership::DecoderCodec::Ogg,0x5000,42,300,10);
    auto ogg_activity=little;ogg_activity.owner=0x5000;ogg_activity.first=300;
    ogg_activity.last=300.4;ogg_activity.epoch=10;ogg_tracker.ObserveDecoded(ogg_activity);
    ogg_tracker.ObservePlayback("ogg-one","spotify:track:ogg",30,300.1);
    ogg_tracker.CompleteCapture(ogg->capture_token,300.5);ogg_buffers.ready.push_back(std::move(ogg));
    Listen ogg_listen;ogg_listen.identity="spotify:track:ogg\x1fogg-one";ogg_listen.eligible=true;
    ogg_listen.playing=true;ogg_listen.start_time=300;ogg_listen.last_time=300.5;
    ogg_listen.last_position=.1;ogg_listen.duration=1;ogg_listen.generation=30;ogg_listen.capture_speed=10;
    Media ogg_current;ogg_current.source=MediaSource::Spotify;ogg_current.spotify_uri="spotify:track:ogg";
    ogg_current.playback_instance="ogg-one";ogg_current.duration=1;ogg_current.playing=true;
    auto ogg_buffering=ogg_current;ogg_buffering.playing=false;ogg_buffering.position=.1;
    ogg_buffering.control_monitoring=true;
    ogg_tracker.ObserveTransition("ogg-one",ogg_buffering.spotify_uri,300.51,false,false);
    auto ogg_next=ogg_current;ogg_next.playback_instance="ogg-two";ogg_next.playing=false;
    ogg_next.control_monitoring=true;
    std::vector<Heard> ogg_heard;
    assert(FinishAppTransition(ogg_buffering,ogg_listen,ogg_current,ogg_heard,ogg_buffers,300.51)&&
           ogg_heard.empty()&&"same-instance buffering establishes monitored Ogg continuity");
    ogg_tracker.ObserveTransition("ogg-two",ogg_next.spotify_uri,300.52,false,false);
    assert(FinishAppTransition(ogg_next,ogg_listen,ogg_current,ogg_heard,ogg_buffers,300.52)&&
           ogg_heard.empty());
    auto ogg_ready=ogg_next;ogg_ready.playing=true;ogg_ready.position=.1;
    assert(FinishPendingNaturalTransition(ogg_ready,ogg_listen,ogg_current,ogg_heard,ogg_buffers)&&
           ogg_heard.size()==1&&ogg_heard[0].provisional&&
           "a complete bound 10x Ogg stream survives the delayed natural-end flag");
    SchedulePublications(ogg_heard,ogg_buffers,10,300.52);SaveJob ogg_job;
    assert(saves.Pop(ogg_job)&&ogg_job.capture->capture_token&&
           ValidateFinalCapture(*ogg_job.capture)==FinalCaptureValidation::Valid);
}
void UnverifiedAacFinalReconciliation() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(11);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=CompleteAacCapture(100,101);const auto duration=capture->Duration();
    capture->capture_token=tracker.BeginCapture(ownership::DecoderCodec::Aac,0,0,100,11);
    tracker.CompleteCapture(capture->capture_token,101);
    assert(tracker.Binding(capture->capture_token).state==ownership::OwnershipState::Invalidated&&
           ValidateFinalCapture(*capture)==FinalCaptureValidation::Valid);
    buffers.ready.push_back(std::move(capture));

    Listen listen;listen.identity="spotify:track:aac\x1fplay-one";listen.eligible=true;
    listen.playing=true;listen.start_time=100;listen.last_time=101.1;
    listen.last_position=1.1;listen.duration=duration;listen.generation=40;listen.capture_speed=10;
    Media current;current.source=MediaSource::Spotify;current.spotify_uri="spotify:track:aac";
    current.playback_instance="play-one";current.duration=duration;current.position=1.1;current.playing=true;
    Media buffering=current;buffering.playing=false;buffering.control_monitoring=true;
    std::vector<Heard> heard;
    assert(FinishAppTransition(buffering,listen,current,heard,buffers,101.2)&&heard.empty()&&
           "a unique complete AAC stream survives monitored same-instance buffering");

    // The next decoder can begin before its playback metadata arrives. It is
    // sequential, not an alternative for the AAC stream that already ended.
    auto successor=std::make_unique<Capture>();successor->born=101.21;
    buffers.ogg_active[{99,42}]=std::move(successor);
    Media next=current;next.playback_instance="play-two";next.playing=false;next.control_monitoring=true;
    assert(FinishAppTransition(next,listen,current,heard,buffers,101.22)&&heard.empty());
    auto ready=next;ready.playing=true;ready.position=.1;
    assert(FinishPendingNaturalTransition(ready,listen,current,heard,buffers)&&heard.size()==1&&
           heard[0].provisional&&"a distinct playback instance closes the retained AAC listen");
    SchedulePublications(heard,buffers,11,101.22);SaveJob job;
    assert(saves.Pop(job)&&job.capture->aac&&job.heard.generation==40&&
           "a non-overlapping successor cannot make the completed AAC association ambiguous");
    publishing_reserved.fetch_sub(job.reserved);

    CaptureBuffers overlap;overlap.ownership_tracker=&tracker;
    overlap.ready.push_back(CompleteAacCapture(200,201));
    auto competing=std::make_unique<Capture>();competing->born=200.5;
    overlap.ogg_active[{100,43}]=std::move(competing);
    Listen blocked=listen;blocked.identity="spotify:track:aac\x1f" "blocked-one";
    blocked.start_time=200;blocked.last_time=201.1;blocked.generation=41;
    Media blocked_current=current;blocked_current.playback_instance="blocked-one";
    Media blocked_buffering=blocked_current;blocked_buffering.playing=false;blocked_buffering.control_monitoring=true;
    std::vector<Heard> rejected;
    assert(!FinishAppTransition(blocked_buffering,blocked,blocked_current,rejected,overlap,201.2)&&
           rejected.empty()&&!blocked.eligible&&
           "an overlapping decoder prevents unverified AAC recovery");
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
        h.provisional=true;return h;
    };
    {
        CaptureBuffers buffers;buffers.ready.push_back(CompleteFlacCapture(100,110));
        auto h=heard();RecentPlaybackHistory recent;recent.Complete(h);
        auto refreshed=h.media;refreshed.title=L"Refreshed title";
        Listen listen;listen.generation=h.generation;listen.identity=h.media.ListenKey();recent.Observe(refreshed,listen);
        std::vector<Heard> pending{h};SaveJob job;
        SchedulePublications(pending,buffers,0,121,&recent);
        assert(saves.Pop(job)&&"complete capture gets a final structural match independent of elapsed time");
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
void CompactFailureIdentity() {
    decoded_sink_target=reinterpret_cast<void*>(0x1000);
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(23);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=std::make_unique<Capture>();capture->mp3=true;capture->born=100;
    constexpr uintptr_t context=0x9000;
    BeginCaptureOwnership(*capture,buffers,ownership::DecoderCodec::Mp3,context,0,100,23);
    buffers.active[context]=std::move(capture);
    ownership::DecodedActivity activity{};activity.owner=context;activity.calls=1;
    activity.produced_floats=4096;activity.first=100.01;activity.last=100.1;activity.epoch=23;
    tracker.ObserveDecoded(activity);
    const std::string old_instance="old-instance";
    tracker.ObservePlayback(old_instance,"spotify:episode:0000000000000000000001",41,100.2);
    Media old;old.artist=L"Old Artist";old.title=L"Old Title";
    RememberBoundCaptureIdentity(buffers,old,41,tracker.PlaybackToken(old_instance));
    const std::string new_instance="new-instance";
    tracker.ObservePlayback(new_instance,"spotify:episode:0000000000000000000002",42,101);
    Media current;current.artist=L"New Artist";current.title=L"New Title";
    RememberBoundCaptureIdentity(buffers,current,42,tracker.PlaybackToken(new_instance));
    diagnostic_lines.clear();const auto failures=failure_events;
    buffers.RecordLostActive(buffers.active[context].get(),ownership::CaptureAbortReason::Timeout);
    assert(failure_events==failures&&
           Contains("final cache check: incomplete or invalid cached audio")&&
           !Contains("New Artist")&&"delayed cleanup names the capture's bound song");

    CaptureBuffers unbound;unbound.active[context]=std::make_unique<Capture>();
    diagnostic_lines.clear();unbound.RecordLostActive(unbound.active[context].get());
    assert(failure_events==failures&&Contains("final cache check: incomplete or invalid cached audio")&&
           "discarded unbound evidence is diagnostic, not a download failure");

    auto required=CompleteFlacCapture(100,110);required->bound_identity="Required Artist - Required Title";
    diagnostic_lines.clear();
    CheckBeforeRelease(*required,"required recording lost",nullptr,ReleaseExpectation::RequiredRecording);
    assert(failure_events==failures+1&&Contains("unmatched cache | Required Artist - Required Title")&&
           "only an established publication obligation becomes a user-visible failure");
    decoded_sink_target=nullptr;
}
void ProductionCaptureQueueLossEpochs() {
    for(const auto kind:{AudioEventKind::FlacBegin,AudioEventKind::AacBegin,AudioEventKind::OggPage}) {
        CaptureBuffers buffers;Slot event{};event.context=0x8888;event.epoch=37;event.time=100;event.kind=kind;
        if(kind==AudioEventKind::FlacBegin)ProcessFlacCapture(event,buffers);
        else if(kind==AudioEventKind::AacBegin)ProcessAacCapture(event,buffers);
        else {
            const auto page=recording_fixture::page(recording_fixture::identification(),0,2,0,41);
            event.length=unsigned(page.size());memcpy(event.bytes,page.data(),page.size());
            ProcessOggCapture(event,buffers);
        }
        assert(buffers.active.size()+buffers.ogg_active.size()==1);
        CaptureQueueLossBatch loss;loss.count=1;loss.entries[0]={event.context,36};
        assert(!ApplyCaptureQueueLosses(buffers,loss,101));
        assert(buffers.active.size()+buffers.ogg_active.size()==1&&"old epoch cannot retire the new decoder lifecycle");
        loss.entries[0].epoch=37;assert(!ApplyCaptureQueueLosses(buffers,loss,101));
        assert(buffers.active.empty()&&buffers.ogg_active.empty()&&
               "queue loss must retire production-created captures after settings epoch changes");
        DrainReleaseAudits();
    }
}
void ContextSpecificCaptureQueueLoss() {
    ResetCaptureQueueLosses();
    {QueueGuard guard;RecordCaptureQueueLossLocked(0xA,7);RecordCaptureQueueLossLocked(0xA,7);}
    auto loss=TakeCaptureQueueLosses();
    assert(!loss.global&&loss.count==1&&loss.entries[0].context==0xA&&loss.entries[0].epoch==7&&
           "repeated overflow on one lifecycle remains one bounded loss record");
    CaptureBuffers buffers;
    auto affected=std::make_unique<Capture>();affected->born=100;affected->capture_context=0xA;affected->capture_epoch=7;
    auto unrelated=std::make_unique<Capture>();unrelated->born=100;unrelated->capture_context=0xB;unrelated->capture_epoch=7;
    buffers.active[0xA]=std::move(affected);buffers.active[0xB]=std::move(unrelated);
    buffers.ready.push_back(CompleteFlacCapture(90,99));
    diagnostic_lines.clear();const auto failures=failure_events;
    assert(!ApplyCaptureQueueLosses(buffers,loss,101));
    assert(!buffers.active.count(0xA)&&buffers.active.count(0xB)==1&&buffers.ready.size()==1&&
           failure_events==failures&&"one decoder's queue loss cannot invalidate unrelated or complete captures");

    for(unsigned i=0;i<4;++i) {
        auto capture=std::make_unique<Capture>();capture->born=110+i;capture->capture_context=0x20+i;
        auto* raw=capture.get();buffers.active[0x20+i]=std::move(capture);
        buffers.RecordLostActive(raw,ownership::CaptureAbortReason::SettingsChange,
            ReleaseExpectation::DiscardedEvidence,true);
        buffers.active.erase(0x20+i);
    }
    assert(!Contains("final cache check")&&"capture drain must not run whole-buffer release audits");
    DrainReleaseAudits();
    assert(Contains("final cache check")&&failure_events==failures&&
           "deferred expected cleanup remains diagnostic-only");

    {QueueGuard guard;for(uintptr_t context=1;context<=33;++context)RecordCaptureQueueLossLocked(context,7);}
    loss=TakeCaptureQueueLosses();
    assert(loss.global&&loss.count==0&&"loss-evidence overflow retains the explicit conservative fallback");
    {QueueGuard guard;RecordCaptureQueueLossLocked(0xA,7);}
    ResetCaptureQueueLosses();loss=TakeCaptureQueueLosses();
    assert(!loss.global&&!loss.count&&"capture lifecycle reset cannot carry old loss into a new epoch");
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
        Heard heard;heard.start=100;heard.finish=110;heard.media.duration=16;heard.provisional=true;
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
void OwnershipCapabilityFallbacks() {
    decoder_ownership_layout={};decoder_ownership_layout.flac_capture_to_decoder=0;
    decoder_ownership_layout_available=true;decoded_sink_target=nullptr;
    {
        ownership::DecoderOwnershipTracker tracker;tracker.Reset(31);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        auto capture=CompleteFlacCapture(100,110);
        BeginCaptureOwnership(*capture,buffers,ownership::DecoderCodec::Flac,0x1000,0,100,31);
        assert(!capture->decoder_owner&&
               "layout discovery without an active decoded hook must not require ownership evidence");
        buffers.ready.push_back(std::move(capture));
        Heard heard;heard.start=100;heard.finish=110;heard.generation=1;heard.media.duration=16;
        heard.media.source=MediaSource::Spotify;heard.media.playback_instance="app-instance";
        std::vector<Heard> pending{heard};SchedulePublications(pending,buffers,0,111);SaveJob job;
        assert(saves.Pop(job)&&"ordinary 1x capture remains publishable after optional decoded-hook failure");
        publishing_reserved.fetch_sub(job.reserved);
    }
    decoded_sink_target=reinterpret_cast<void*>(0x2000);
    {
        ownership::DecoderOwnershipTracker tracker;tracker.Reset(32);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        auto capture=CompleteFlacCapture(100,110);
        BeginCaptureOwnership(*capture,buffers,ownership::DecoderCodec::Flac,0x3000,0,100,32);
        ownership::DecodedActivity activity{};activity.owner=0x3000;activity.calls=1;
        activity.produced_floats=4096;activity.first=100.1;activity.last=109.9;activity.epoch=32;
        tracker.ObserveDecoded(activity);tracker.CompleteCapture(capture->capture_token,110);
        Heard heard;heard.start=100;heard.finish=110;heard.generation=1;heard.media.duration=16;
        heard.media.source=MediaSource::Windows;
        tracker.ObservePlayback("",heard.media.spotify_uri,heard.generation,110.1);
        assert(tracker.Binding(capture->capture_token).state==ownership::OwnershipState::Bound);
        buffers.ready.push_back(std::move(capture));
        std::vector<Heard> pending{heard};SchedulePublications(pending,buffers,0,111);SaveJob job;
        assert(saves.Pop(job)&&"Windows-only playback remains publishable without an app instance ID");
        publishing_reserved.fetch_sub(job.reserved);
    }
    decoded_sink_target=nullptr;decoder_ownership_layout={};decoder_ownership_layout_available=false;
}
void AppTailLifecycle() {
    CaptureBuffers buffers;std::vector<Heard> heard;Listen listen;Media current,media;
    media.source=MediaSource::Spotify;media.spotify_uri="spotify:track:0000000000000000000001";
    media.title=L"Previous";media.duration=10;media.playing=true;
    for(unsigned i=0;i<=19;++i){media.position=i*.5;AssociateListen(media,listen,current,heard,buffers,100+i*.5);}
    auto active=std::make_unique<Capture>();active->lossless=true;active->born=100;active->flac.rate=1;active->coverage.samples=9;buffers.active[1]=std::move(active);
    Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:0000000000000000000002";
    FinishAppTransition(next,listen,current,heard,buffers,110);
    assert(heard.size()==1&&buffers.active.count(1));
    auto late=std::move(buffers.active[1]);buffers.active.erase(1);late->finished=121;late->coverage.samples=10;buffers.ready.push_back(std::move(late));
    SchedulePublications(heard,buffers,0,121);SaveJob job;assert(saves.Pop(job));
    publishing_reserved.fetch_sub(job.reserved);
}
void ReadyDurationCorroboratesDecoder() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(12);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=std::make_unique<Capture>();capture->lossless=true;capture->metadata=true;
    capture->decoder_owner=0x7200;capture->flac.rate=44100;
    capture->flac.total_samples=10253165;capture->coverage.frames=1;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,12,10);
    ownership::DecodedActivity decoded{};decoded.owner=capture->decoder_owner;
    decoded.calls=1;decoded.produced_floats=8192;decoded.first=100.1;
    decoded.last=100.2;decoded.epoch=12;decoded.sequence=11;
    tracker.ObserveDecoded(decoded);buffers.active[1]=std::move(capture);
    tracker.ObserveTransition("new-playback","spotify:track:new",100.3,false,true);

    Media stale;stale.source=MediaSource::Windows;stale.spotify_uri="spotify:track:old";
    stale.duration=133.565;stale.playing=true;
    assert(!BindReadyCapture(buffers,stale,1,100.4));
    assert(tracker.Binding(buffers.active[1]->capture_token).state==
           ownership::OwnershipState::AwaitingPlaybackInstance);

    Media ready;ready.source=MediaSource::Windows;
    ready.duration=232.498;ready.playing=true;
    const auto playback=BindReadyCapture(
        buffers,ready,2,100.5,0,"spotify:track:new");
    const auto binding=tracker.Binding(buffers.active[1]->capture_token);
    assert(playback&&binding.state==ownership::OwnershipState::Bound&&
           binding.listen_generation==2&&binding.playback_token==playback);
}
void ReadyUpgradesPreboundPlaybackGeneration() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(42);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=std::make_unique<Capture>();capture->lossless=true;capture->metadata=true;
    capture->decoder_owner=0x7600;capture->flac.rate=44100;
    capture->flac.total_samples=8930250;capture->coverage.frames=1;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,42,10);
    tracker.ObserveTransition("outgoing-id","spotify:track:oxygen",100.1,false,false);
    ownership::DecodedActivity decoded{};decoded.owner=capture->decoder_owner;
    decoded.calls=1;decoded.produced_floats=8192;decoded.first=100.2;
    decoded.last=100.3;decoded.epoch=42;decoded.sequence=11;
    tracker.ObserveDecoded(decoded);
    tracker.ObserveTransition("outgoing-id","spotify:track:oxygen",100.35,false,false);
    decoded.first=100.4;decoded.last=100.45;decoded.sequence=12;
    tracker.ObserveDecoded(decoded);
    assert(tracker.Binding(capture->capture_token).state==ownership::OwnershipState::Bound&&
           !tracker.Binding(capture->capture_token).listen_generation);
    const auto token=tracker.Binding(capture->capture_token).capture_token;
    buffers.active[1]=std::move(capture);
    Media ready;ready.source=MediaSource::Spotify;ready.spotify_uri="spotify:track:oxygen";
    ready.playback_instance="outgoing-id";ready.duration=202.5;ready.playing=true;
    const auto playback=BindReadyCapture(buffers,ready,77,100.5);
    assert(playback&&tracker.Binding(token).listen_generation==77&&
           "Ready must attach the listen generation to its prebound playback stage");
    Heard heard;heard.start=121;heard.finish=122;heard.generation=77;heard.media.duration=202.5;
    assert(buffers.active[1]->listen_generation==77&&
           CaptureTimingMatchesListen(*buffers.active[1],heard)&&
           "proven ownership must bypass the capability-fallback timing window");
}
void WindowsReadyNaturalAppTransitionPublishes() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(13);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto capture=CompleteFlacCapture(100,116);capture->decoder_owner=0x7300;
    capture->capture_token=tracker.BeginCapture(
        ownership::DecoderCodec::Flac,capture->decoder_owner,0,100,13,10);
    ownership::DecodedActivity decoded{};decoded.owner=capture->decoder_owner;
    decoded.calls=1;decoded.produced_floats=8192;decoded.first=100.1;
    decoded.last=115.9;decoded.epoch=13;decoded.sequence=11;
    tracker.ObserveDecoded(decoded);tracker.CompleteCapture(capture->capture_token,116,12);
    buffers.ready.push_back(std::move(capture));
    tracker.ObserveTransition("old-instance","spotify:track:old",100.2,false,false);

    Media current;current.source=MediaSource::Windows;current.spotify_uri="spotify:track:old";
    current.title=L"Windows title";current.artist=L"Artist";current.album=L"Album";
    current.duration=16;current.position=15.5;current.playing=true;
    const auto playback=BindReadyCapture(buffers,current,1,100.3,0,"spotify:track:old");
    assert(playback);
    Listen listen;listen.identity=current.ListenKey();listen.generation=1;listen.eligible=true;
    listen.playing=true;listen.start_time=100;listen.last_time=115.5;
    listen.last_position=15.5;listen.duration=16;listen.capture_speed=1;

    Media next;next.source=MediaSource::Spotify;next.spotify_uri="spotify:track:new";
    next.playback_instance="new-instance";next.natural_transition=true;
    next.control_monitoring=true;
    tracker.ObserveTransition(next.playback_instance,next.spotify_uri,116,false,true);
    std::vector<Heard> heard;
    FinishAppTransition(next,listen,current,heard,buffers,116);
    assert(heard.size()==1&&heard[0].generation==1&&
           "a complete bound Windows listen accepts the app's monitored natural end");
    SchedulePublications(heard,buffers,13,116);SaveJob job;
    assert(saves.Pop(job)&&job.heard.generation==1);
    publishing_reserved.fetch_sub(job.reserved);
}
void TransitionPublicationMatrixStress() {
    constexpr unsigned iterations=5000;
    std::mt19937_64 random(4043931417ULL);
    const unsigned speeds[]={1,2,4,8,10};
    for(unsigned run=0;run<iterations;++run) {
        SaveJob leftover;assert(!saves.Pop(leftover));
        const auto speed=speeds[random()%5],style=unsigned(random()%3),failure=unsigned(random()%5);
        const double base=1000+run*100.0,end=base+16.0/speed;
        const auto generation=std::uint64_t(run+1);
        CaptureBuffers buffers;std::vector<Heard> heard;
        Media current;current.source=MediaSource::Spotify;
        current.spotify_uri="spotify:track:0000000000000000000001";
        current.playback_instance="play-a-"+std::to_string(run);
        current.title=L"A";current.artist=L"Artist";current.album=L"Album";
        current.duration=16;current.playing=true;
        Listen listen;listen.identity=current.ListenKey();listen.eligible=true;listen.playing=true;
        listen.start_time=base;listen.last_time=end;listen.duration=16;
        listen.last_position=(random()%2)?15.8:std::min(15.8,16.0/speed);
        listen.generation=generation;listen.capture_speed=speed;
        current.position=listen.last_position;
        buffers.ready.push_back(CompleteFlacCapture(base,end));

        Media transition;transition.source=MediaSource::Spotify;
        transition.spotify_uri="spotify:track:0000000000000000000002";
        transition.playback_instance="play-b-"+std::to_string(run);
        transition.control_monitoring=true;
        if(failure==1)transition.explicit_skip=transition.explicit_control=true;
        else if(failure==2)transition.explicit_control=true;
        else if(failure==3) {
            transition=current;transition.position=13;transition.control_monitoring=true;
        } else if(failure==4)buffers.ready.push_back(CompleteFlacCapture(base,end));

        const bool valid_transition=failure==0||failure==4;
        if(valid_transition) {
            if(style==0)transition.natural_transition=true;
            else {
                transition.transition_pending=true;
                transition.playback_instance=style==1?"":current.playback_instance;
            }
        }
        const bool held=FinishAppTransition(transition,listen,current,heard,buffers,end);
        if(valid_transition&&style) {
            assert(held&&buffers.pending_natural_generation==generation);
            if(style==2) {
                auto stale=current;stale.position=std::min(15.9,current.position+.05);
                assert(!FinishPendingNaturalTransition(stale,listen,current,heard,buffers)&&
                       buffers.pending_natural_generation==generation);
            }
            Media ready=transition;ready.transition_pending=false;ready.playback_instance="play-b-"+std::to_string(run);
            ready.title=L"B";ready.artist=L"Artist";ready.album=L"Album";
            ready.position=0;ready.duration=16;ready.playing=true;
            assert(FinishPendingNaturalTransition(ready,listen,current,heard,buffers));
        }
        SchedulePublications(heard,buffers,0,end);SaveJob job;
        const bool saved=saves.Pop(job);
        assert(saved==(!failure));
        if(saved) {
            assert(job.heard.media.spotify_uri==current.spotify_uri&&
                   job.heard.generation==generation&&
                   ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid);
            publishing_reserved.fetch_sub(job.reserved);
        }
        assert(!saves.Pop(job));
    }
}
void BoundWindowsIdentitySurvivesMetadataRefinement() {
    Media first;first.source=MediaSource::Windows;first.title=L"Stale outgoing title";
    first.artist=L"Old artist";first.duration=160.774;first.position=.150;first.playing=true;
    Listen listen;listen.identity=first.ListenKey();listen.generation=7;
    listen.eligible=true;listen.playing=true;listen.duration=first.duration;
    listen.last_position=first.position;listen.last_time=100;

    CanonicalizeBoundReadyIdentity(
        first,listen,"spotify:track:incoming",4);
    assert(first.spotify_uri=="spotify:track:incoming"&&
           listen.identity=="spotify:track:incoming"&&
           "a proven decoder binding promotes the app URI to the listen identity");

    Media refined;refined.source=MediaSource::Windows;refined.title=L"Turn It Up";
    refined.artist=L"Keanu Silva";refined.duration=160.774;
    refined.position=.751;refined.playing=true;
    CarryBoundReadyIdentity(refined,first,false);
    const auto generation=listen.generation;
    std::vector<Heard> heard;CaptureBuffers buffers;Media current=first;
    AssociateListen(refined,listen,current,heard,buffers,100.6);
    assert(listen.generation==generation&&listen.eligible&&heard.empty()&&
           "a title correction inside one proven playback lifecycle cannot orphan its capture binding");

    Media successor;successor.source=MediaSource::Windows;successor.title=L"Next";
    successor.duration=refined.duration;successor.position=0;successor.playing=true;
    CarryBoundReadyIdentity(successor,current,true);
    assert(successor.spotify_uri.empty()&&
           "a registered successor transition prevents URI inheritance from the outgoing lifecycle");
}
void MissingPlaybackWarning() {
    CaptureBuffers buffers;auto complete=std::make_unique<Capture>();complete->finished=10;
    buffers.ready.push_back(std::move(complete));MediaDiagnostic diagnostic;
    diagnostic.stage="select-session";diagnostic.reason="no-spotify-session";
    diagnostic_lines.clear();const auto failures=failure_events;
    MediaWarningState warning;diagnostic_lines.clear();
    ReportMediaUnavailable(buffers,true,diagnostic,100,warning);
    ReportMediaUnavailable(buffers,true,diagnostic,500,warning);
    assert(diagnostic_lines.empty()&&failure_events==failures&&
           "a retained complete capture cannot fail merely because Ready metadata is temporarily unavailable");
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
void AdvertisementStageIsNotDownloadFailure() {
    CaptureBuffers buffers;MediaDiagnostic diagnostic;
    diagnostic.reason="client-active-advertisement";
    diagnostic_lines.clear();const auto failures=failure_events;
    MarkUnidentifiedInterstitial(buffers,diagnostic.reason);
    MarkUnidentifiedInterstitial(buffers,diagnostic.reason);
    assert(buffers.unidentified_interstitial_active&&
           diagnostic_lines.size()==1&&Contains("advertisement")&&Contains("excluded")&&
           "an advertisement stage is named once and excluded from downloads");
    auto complete=std::make_unique<Capture>();complete->finished=10;
    buffers.ready.push_back(std::move(complete));
    MediaWarningState warning;
    ReportMediaUnavailable(buffers,true,diagnostic,20,warning);
    assert(failure_events==failures&&diagnostic_lines.size()==1&&
           "expected advertisement audio cannot be reported as a song download failure");
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
void PodcastCatalog() {
    Media episode;episode.spotify_uri="spotify:episode:0000000000000000000001";
    episode.title=L"Episode";episode.artist=L"Publisher";episode.album=L"Show";episode.duration=60;
    const auto catalog=CatalogFor(episode);
    assert(catalog.kind==MediaKind::Podcast&&catalog.show==L"Show"&&catalog.title==L"Episode");
    assert(RelativePath(catalog,L".mp3")==L"Podcasts\\Show\\Episode.mp3");
}
void PublicationLogging() {
    assert(PublicationLogKind(Publication::Saved,true)==LogKind::Finished);
    assert(PublicationLogKind(Publication::Saved,false)==LogKind::Activity);
    assert(PublicationLogKind(Publication::Skipped,false)==LogKind::Activity);
    assert(PublicationLogKind(Publication::Failed,false)==LogKind::Failure);
}
}
namespace history {
std::uint64_t MetadataRevision(){return 0;}
void CaptureLogEvent(LogKind kind,const char*,const std::string& message){
    if(!LoggingEnabled()||(kind==LogKind::Diagnostic&&!DebugLoggingEnabled()))return;
    diagnostic_lines.push_back(message);if(kind==LogKind::Failure)++failure_events;
}
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media&,Catalog&){}
void EnrichTags(const Media&,Tags&){}
}
int wmain(int argc,wchar_t** argv) {
    // Use in-memory preferences only. Never read or write the user's INI/music.
    InterlockedExchange(&history::log_level_value,4);
    if(argc==2&&!wcscmp(argv[1],L"--queue-epoch")) {ProductionCaptureQueueLossEpochs();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--podcast-end")) {PodcastEndWithoutReadySuccessor();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--pending-prior-control-stdin")) {
        const std::string payload(std::istreambuf_iterator<char>(std::cin),{});
        ClientPlaybackRecord record;std::string error;
        assert(ParseClientPlayback(payload,record,error)&&record.prior_control);
        Media current;current.source=MediaSource::Spotify;
        current.spotify_uri="spotify:track:0000000000000000000001";
        current.playback_instance="playback-1";current.duration=180;current.playing=true;
        Listen listen;listen.identity=current.ListenKey();listen.eligible=true;listen.playing=true;
        listen.generation=1;listen.start_time=100;listen.last_time=101;listen.last_position=179;listen.duration=180;
        CaptureBuffers buffers;buffers.pending_natural_generation=1;
        buffers.pending_natural_time=101;buffers.pending_natural_listen=listen;
        Media next;next.source=MediaSource::Spotify;next.spotify_uri=record.metadata.uri;
        next.playback_instance=record.instance;next.duration=record.metadata.duration;
        next.position=record.position;next.playing=record.playing;next.prior_control=record.prior_control;
        std::vector<Heard> heard;
        assert(!FinishPendingNaturalTransition(next,listen,current,heard,buffers)&&
               !buffers.pending_natural_generation&&!listen.eligible&&heard.empty());
        puts("PASS: real collector prior-control Ready cancels native pending recovery");
        return 0;
    }
    assert(argc==1);
    Connectivity();PodcastCatalog();PublicationLogging();PodcastCaptureEvents();PodcastEndWithoutReadySuccessor();DirectIdentityRetriesAndIgnoresLogVerbosity();ReadyUpgradesPreboundPlaybackGeneration();EmptyPlaybackIdNaturalTransitionIsRetried();UnclassifiedMonitoredTransitionWaitsForReady();ConcurrentOggLogicalStreams();FinalCaptureRevalidation();CrossfadeQuarantineRecovery();BoundNaturalTransitionUsesFinalCaptureEvidence();DjVoiceDoesNotBlockMusicStart();CurrentItemChangeReconcilesDjSuccessor();CurrentItemChangeBeforeStreamOutputReconcilesDjSuccessor();AcceleratedUiClockRecovery();DecoderOwnershipFinalReconciliation();UnverifiedAacFinalReconciliation();KnownDurationAlternativeIsNotAmbiguous();AppIdentityAndSourceChanges();NaturalAppBufferingTransition();RecentPlaybackMatching();FinalReleaseReconciliation();CompactFailureIdentity();ProductionCaptureQueueLossEpochs();ContextSpecificCaptureQueueLoss();DurableAssociationEvidence();OwnershipCapabilityFallbacks();AppTailLifecycle();ReadyDurationCorroboratesDecoder();WindowsReadyNaturalAppTransitionPublishes();TransitionPublicationMatrixStress();BoundWindowsIdentitySurvivesMetadataRefinement();MissingPlaybackWarning();AdvertisementStageIsNotDownloadFailure();ObservePlayback();CoverageAndFailures();SameContextFlacRestartBeginsFreshCapture();CaptureOwnershipLifecycle();OwnershipDiagnosticPrivacy();HookBoundaries();DecodedActivityQueue();TimingVerbosity();BasicModeProfiling();LoggingDisabled();
    puts("PASS: observed transitions, one FLAC failure context/outcome, original callback semantics, profiling-only intervals, basic-mode zero profiling and changed-only NLM observations");
    return 0;
}
