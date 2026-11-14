// Exercise real hooks/worker diagnostics without patching or controlling Spotify.
#include "../native/async_log.h"
#include "../native/client_playback.h"
#include "../native/retry_schedule.h"
#include <atomic>
namespace {
std::atomic<bool> fail_retry_node{false};
std::atomic<unsigned> retry_node_failures{0};
}
namespace history {
template<class Job> class FaultableRetrySchedule {
    RetrySchedule<Job> schedule_;
public:
    void Push(Job&& job,uint64_t deadline) {
        if(fail_retry_node.exchange(false)) {++retry_node_failures;throw std::bad_alloc();}
        schedule_.Push(std::move(job),deadline);
    }
    std::optional<Job> PopDue(uint64_t now){return schedule_.PopDue(now);}
    std::optional<uint64_t> NextDeadline() const{return schedule_.NextDeadline();}
};
}
namespace history {void CaptureLogEvent(LogKind,const char*,const std::string&);}
#define LogEvent CaptureLogEvent
#define RetrySchedule FaultableRetrySchedule
#include "../native/audio_history.cpp"
#undef RetrySchedule
#undef LogEvent
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include "spac_fixture.h"
#include <cassert>
#include <iostream>
#include <random>
#include <filesystem>
#include <fstream>
#include <ogg/ogg.h>

namespace {
std::vector<std::string> diagnostic_lines;
unsigned failure_events=0;
bool Contains(const char* text) {
    return std::any_of(diagnostic_lines.begin(),diagnostic_lines.end(),[text](const auto& line){return line.find(text)!=std::string::npos;});
}
unsigned init_calls=0,read_calls=0,frame_calls=0,error_calls=0;
int read_status=0,frame_status=0;
bool oversized_read=false,empty_read=false;
bool invalid_decoded_span=false,advance_dump_epoch=false;
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
    encoded->size-=decode_consumed;if(advance_dump_epoch)InterlockedIncrement(&history::capture_epoch);
    SetLastError(1234);return result;
}
std::vector<uint8_t> PodcastMp3() {
    std::vector<uint8_t> out={'I','D','3',4,0,0,0,0,0,0};
    const auto add=[&](unsigned length,uint8_t third) {
        const auto at=out.size();out.resize(at+length);out[at]=0xff;out[at+1]=0xfb;out[at+2]=third;out[at+3]=0;
    };
    add(417,0x90);add(523,0xa2);add(417,0x90);return out;
}
void DumpStreamHooks() {
    const auto prior_level=InterlockedExchange(&history::log_level_value,1);
    Settings preferences;preferences.dump_streams=true;
    assert(NeedPlaybackIdentity(spotify::DiscoveryResult::Found,preferences));
    assert(!NeedPlaybackIdentity(spotify::DiscoveryResult::MissingTarget,preferences));
    preferences.dump_streams=false;assert(NeedPlaybackIdentity(spotify::DiscoveryResult::Found,preferences)&&
        "decoder reuse boundaries must remain available at normal logging with dumps disabled");
    InterlockedExchange(&history::log_level_value,prior_level);
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::filesystem::path(temp)/(L"Floggfy-dump-hooks-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    assert(stream_dump::Start(root.wstring()));const auto folder=stream_dump::SessionFolder();
    const auto prior_decode=original_decoded_process;const auto prior_calls=decode_calls;
    const auto prior_consumed=decode_consumed;const auto prior_invalid=invalid_decoded_span;const auto prior_epoch=CaptureEpoch();
    original_decoded_process=Decode;decode_consumed=100;
    unsigned char input[256]{};for(unsigned i=0;i<256;++i)input[i]=static_cast<unsigned char>(i);
    float samples[160]{};
    for(const bool invalid:{false,true}) {
        invalid_decoded_span=invalid;advance_dump_epoch=invalid;
        decoded_output::Span decoded{samples,160},encoded{input,256};void* result=reinterpret_cast<void*>(0x89);
        assert(DecodedSinkHook(reinterpret_cast<void*>(0x7000),result,&decoded,&encoded,true)==result&&GetLastError()==1234);
    }
    assert(stream_dump::Stop());unsigned files=0;
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".bin") {
        std::ifstream file(entry.path(),std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(file),{}};
        assert(bytes.size()==200&&std::equal(bytes.begin(),bytes.begin()+100,reinterpret_cast<const char*>(input))&&
               std::equal(bytes.begin()+100,bytes.end(),reinterpret_cast<const char*>(input)));++files;
    }
    assert(files==1);
    original_decoded_process=prior_decode;decode_calls=prior_calls;decode_consumed=prior_consumed;invalid_decoded_span=prior_invalid;advance_dump_epoch=false;InterlockedExchange(&history::capture_epoch,LONG(prior_epoch));
    ResetDecodedActivityQueue();
    puts("PASS: real decoder hook dumps exact consumed bytes even when decoded output is rejected, preserving original returns and LastError");
}
std::string JsonString(const std::string& line,const char* name) {
    const std::string key=std::string{"\""}+name+"\":\"";const auto begin=line.find(key);
    assert(begin!=std::string::npos);const auto value=begin+key.size(),end=line.find('"',value);
    assert(end!=std::string::npos);return line.substr(value,end-value);
}
std::uint64_t JsonUnsigned(const std::string& line,const char* name) {
    const std::string key=std::string{"\""}+name+"\":";const auto begin=line.find(key);
    assert(begin!=std::string::npos);const auto value=begin+key.size();
    return std::stoull(line.substr(value));
}
unsigned HexNibble(char value) {
    if(value>='0'&&value<='9')return unsigned(value-'0');
    if(value>='a'&&value<='f')return unsigned(value-'a'+10);
    if(value>='A'&&value<='F')return unsigned(value-'A'+10);
    assert(false&&"invalid playback identity hex");return 0;
}
// Local dump replay: preserve callback/metadata delivery order and the original
// observation clocks. Audio stays outside the repository.
void ReplayCapturedOggListen(const wchar_t* session_path,const wchar_t* wanted_uri) {
    const auto session=std::filesystem::path(session_path);
    std::ifstream metadata{session};assert(metadata);
    const auto wanted=Utf8(wanted_uri);
    const auto number=[](const std::string& line,const char* name) {
        const auto key=std::string{"\""}+name+"\":";
        const auto at=line.find(key);assert(at!=std::string::npos);
        return std::stod(line.substr(at+key.size()));
    };
    struct Input {
        ogg_sync_state sync{};std::vector<uint8_t> bytes;
        std::vector<std::vector<uint8_t>> logical_streams;
        size_t consumed=0;unsigned pages=0;bool collecting=false;
    };
    std::map<unsigned,Input> inputs;std::map<std::string,unsigned> instances;
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    decoded_sink_target=reinterpret_cast<void*>(0x1000);
    playback_pump_target=reinterpret_cast<void*>(0x2000);
    decoder_ownership_layout={};decoder_ownership_layout.ogg_capture_to_decoder=-136;
    decoder_ownership_layout_available=true;
    uint64_t recording_generation=0;Media current;
    bool seen=false,published=false;std::string line;
    const auto owner=[](unsigned stream){return uintptr_t(0x10000+stream*0x1000);};
    const auto bind=[&](double time) {
        const auto match=instances.find(current.playback_instance);
        if(match==instances.end())return;
        std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
        matches[0].metadata_generation=recording_generation;
        matches[0].role=playback_identity::SnapshotRole::PlayerPlaybackId;
        matches[0].decoder=owner(match->second);
        const auto token=BindDirectReadyCapture(buffers,current,recording_generation,time,matches,1);
        RememberBoundCaptureIdentity(buffers,current,recording_generation,token);
    };
    while(std::getline(metadata,line)) {
        const auto kind=JsonString(line,"kind");
        if(kind=="gap")assert(false&&"cannot prove a complete recording from a dump with gaps");
        if(kind!="input"&&kind!="metadata"&&kind!="playback")continue;
        const auto time=number(line,"started_mono");
        if(kind=="input") {
            const auto stream=unsigned(JsonUnsigned(line,"stream"));
            auto& input=inputs[stream];
            if(input.bytes.empty()) {
                std::ifstream raw(session.parent_path()/JsonString(line,"file"),std::ios::binary);
                input.bytes={std::istreambuf_iterator<char>(raw),{}};assert(!input.bytes.empty());
                assert(ogg_sync_init(&input.sync)==0);
            }
            const auto offset=size_t(JsonUnsigned(line,"offset")),size=size_t(JsonUnsigned(line,"bytes"));
            assert(offset==input.consumed&&offset+size<=input.bytes.size());
            auto* data=ogg_sync_buffer(&input.sync,long(size));assert(data);
            memcpy(data,input.bytes.data()+offset,size);assert(ogg_sync_wrote(&input.sync,long(size))==0);
            input.consumed+=size;ogg_page page{};
            while(ogg_sync_pageout(&input.sync,&page)>0) {
                Slot slot{};slot.kind=AudioEventKind::OggPage;slot.context=owner(stream)+136;
                // Raw callbacks can read ahead of libogg. Recreate valid page
                // ordering within the callback; only metadata clocks are exact.
                slot.time=time+(++input.pages)*1e-6;slot.capture=true;slot.length=unsigned(page.header_len+page.body_len);
                assert(slot.length<=sizeof(slot.bytes));
                memcpy(slot.bytes,page.header,size_t(page.header_len));
                memcpy(slot.bytes+page.header_len,page.body,size_t(page.body_len));
                Page parsed;
                if(ParsePage(slot.bytes,slot.length,parsed)) {
                    if(parsed.vorbis_start){input.logical_streams.emplace_back();input.collecting=true;}
                    if(input.collecting) {
                        auto& source=input.logical_streams.back();
                        source.insert(source.end(),slot.bytes,slot.bytes+slot.length);
                        if(parsed.flags&4)input.collecting=false;
                    }
                }
                ProcessOggCapture(slot,buffers);
                ownership::DecodedActivity activity{};activity.owner=owner(stream);
                activity.calls=1;activity.produced_floats=2048;activity.first=slot.time;activity.last=slot.time;
                tracker.ObserveDecoded(activity);
            }
        } else if(kind=="playback") {
            instances[JsonString(line,"instance")]=unsigned(JsonUnsigned(line,"stream"));bind(time);
        } else {
            Media media;media.source=MediaSource::Spotify;
            media.spotify_uri=JsonString(line,"uri");media.playback_instance=JsonString(line,"instance");
            media.position=number(line,"position");media.duration=number(line,"duration");
            media.playing=line.find("\"playing\":true")!=std::string::npos;
            const auto state=JsonString(line,"state");media.control_monitoring=true;
            if(state=="ready") {
                const auto title=JsonString(line,"title");media.title={title.begin(),title.end()};
                recording_generation=ObserveRecordingMetadata(media,buffers,current,time);bind(time);
                if(media.spotify_uri==wanted)seen=true;
            } else if(state=="buffering") {
                media.natural_transition=!current.playback_instance.empty()&&
                    !media.playback_instance.empty()&&media.playback_instance!=current.playback_instance;
                media.transition_pending=!media.natural_transition;
                tracker.ObserveTransition(media.playback_instance,media.spotify_uri,time,false,media.natural_transition);
            }
            ScheduleStreamPublications(buffers,0,time);SaveJob job;
            while(saves.Pop(job)) {
                if(job.heard.media.spotify_uri==wanted) {
                    assert(ValidateFinalCapture(*job.capture)==FinalCaptureValidation::Valid);
                    bool exact=false;
                    for(const auto& input:inputs)for(const auto& source:input.second.logical_streams) {
                        if(source.size()!=job.capture->data.size())continue;
                        bool equal=true;
                        for(size_t at=0;at<source.size();++at)if(source[at]!=job.capture->data[at]){equal=false;break;}
                        exact=exact||equal;
                    }
                    assert(exact&&"publication must retain the exact original logical Ogg stream");
                    published=true;
                }
                publishing_reserved.fetch_sub(job.reserved);
            }
            if(seen&&state=="ready"&&media.spotify_uri!=wanted)break;
        }
    }
    for(auto& entry:inputs)ogg_sync_clear(&entry.second.sync);
    for(const auto& entry:diagnostic_lines)std::cout<<entry<<'\n';
    std::cout.flush();
    assert(seen&&published&&"the complete real Ogg recording must survive its recorded app transitions");
    std::puts("PASS: real Ogg session bytes and recorded metadata delivery order produce a validated save");
}
void ReplayCapturedMp3(const wchar_t* raw_path,const wchar_t* metadata_path) {
    std::ifstream raw_file(std::filesystem::path(raw_path),std::ios::binary);
    const std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(raw_file),{}};
    std::ifstream metadata{std::filesystem::path(metadata_path)};
    assert(!raw.empty()&&metadata);

    const auto prior_pump=original_playback_pump;const auto prior_offset=playback_pump_decoder_offset;
    original_playback_pump=[](void*,void*,void*)->int{return 17;};playback_pump_decoder_offset=0x70;
    queue=new Slot[capacity];event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    head=tail=count=0;ResetMp3Probes();ResetCaptureQueueLosses();ResetPlaybackIdentityBridge();ResetDecodedActivityQueue();

    constexpr uintptr_t decoder=0x9870;const auto epoch=CaptureEpoch();CaptureBuffers buffers;
    std::array<unsigned char,0x100> owner{};std::array<unsigned char,0x200> provider{};
    std::array<unsigned char,0xa00> state{};const auto provider_ptr=reinterpret_cast<uintptr_t>(provider.data());
    const auto state_ptr=reinterpret_cast<uintptr_t>(state.data());
    memcpy(owner.data()+0x70,&decoder,sizeof(decoder));memcpy(owner.data()+0x78,&provider_ptr,sizeof(provider_ptr));
    memcpy(provider.data()+0x1e8,&state_ptr,sizeof(state_ptr));
    const auto drain=[&]{Slot slot{};while(Pop(slot))ProcessMp3Capture(slot,buffers);};

    std::string line;std::size_t input_end=0,last_marker=0;unsigned playback_markers=0;
    double time=100;
    while(std::getline(metadata,line)) {
        if(line.find("\"kind\":\"gap\"")!=std::string::npos)assert(false&&"captured replay contains an input gap");
        if(line.find("\"kind\":\"playback\"")!=std::string::npos) {
            const auto offset=std::size_t(JsonUnsigned(line,"offset"));const auto instance=JsonString(line,"instance");
            assert(offset==input_end&&instance.size()==32);
            for(unsigned index=0;index<16;++index)state[0x9b8+index]=
                static_cast<unsigned char>((HexNibble(instance[index*2])<<4)|HexNibble(instance[index*2+1]));
            assert(PlaybackPumpHook(owner.data(),nullptr,nullptr)==17);drain();last_marker=offset;++playback_markers;
        } else if(line.find("\"kind\":\"input\"")!=std::string::npos) {
            const auto offset=std::size_t(JsonUnsigned(line,"offset"));const auto bytes=std::size_t(JsonUnsigned(line,"bytes"));
            assert(offset==input_end&&bytes<=raw.size()-offset);
            QueueMp3Encoded(decoder,raw.data()+offset,bytes,time,epoch);drain();input_end+=bytes;time+=.001;
        }
    }
    assert(playback_markers>=3&&input_end==raw.size()&&last_marker<raw.size());
    const auto active=buffers.active.find(decoder);assert(active!=buffers.active.end());
    const auto expected=raw.size()-last_marker;
    assert(active->second->bytes==expected&&active->second->data.size()==expected&&
           "the reused decoder must retain only the stream after the final native playback boundary");
    for(std::size_t index=0;index<expected;++index)assert(active->second->data[index]==raw[last_marker+index]);
    Mp3Info info;std::string error;assert(InspectMp3Source(active->second->data,info,error)&&info.complete);
    assert(buffers.ready.size()==1&&"the completed first stream survives while the interrupted replay is discarded");

    ResetMp3Probes();ResetCaptureQueueLosses();ResetPlaybackIdentityBridge();
    CloseHandle(event);event=nullptr;delete[] queue;queue=nullptr;head=tail=count=0;
    original_playback_pump=prior_pump;playback_pump_decoder_offset=prior_offset;
    std::cout<<"PASS: captured callback replay retained "<<expected<<" successor bytes across "
             <<playback_markers<<" native playback identities (duration="<<info.Duration()<<")\n";
}
void ReplayCapturedQuality(const wchar_t* raw_path,const wchar_t* metadata_path,
                           const wchar_t* field_hex,const wchar_t* wanted_uri) {
    std::ifstream raw_file(std::filesystem::path(raw_path),std::ios::binary);
    const std::vector<uint8_t> raw{std::istreambuf_iterator<char>(raw_file),{}};
    std::ifstream metadata{std::filesystem::path(metadata_path)};
    std::string field=Utf8(field_hex);const std::string uri=Utf8(wanted_uri);
    assert(raw.size()>32768&&metadata);
    Media media;media.source=MediaSource::Spotify;std::string line;
    while(std::getline(metadata,line))if(line.find("\"kind\":\"metadata\"")!=std::string::npos&&
        JsonString(line,"uri")==uri&&line.find("\"state\":\"ready\"")!=std::string::npos&&
        line.find("\"playing\":true")!=std::string::npos) {
        media.spotify_uri=uri;media.playback_instance=JsonString(line,"instance");
        media.title=L"Captured episode";media.duration=double(JsonUnsigned(line,"duration"));media.playing=true;
    }
    assert(!media.playback_instance.empty());
    if(field=="auto") {
        metadata.clear();metadata.seekg(0);field.clear();
        while(std::getline(metadata,line))if(line.find("\"kind\":\"playback\"")!=std::string::npos&&
            line.find("\"native_field\":")!=std::string::npos&&JsonString(line,"instance")==media.playback_instance)
            field=JsonString(line,"native_field");
    }
    assert(field.size()==64);
    queue=new Slot[capacity];event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    head=tail=count=0;ResetMp3Probes();ResetCaptureQueueLosses();ResetPlaybackIdentityBridge();ResetDecodedActivityQueue();
    constexpr uintptr_t decoder=0x9870;const auto epoch=CaptureEpoch();
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(epoch);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    decoded_sink_target=reinterpret_cast<void*>(0x1000);playback_pump_target=reinterpret_cast<void*>(0x2000);
    original_playback_pump=[](void*,void*,void*)->int{return 17;};playback_pump_decoder_offset=0x70;
    std::array<unsigned char,0x100> owner{};std::array<unsigned char,0x200> provider{};
    std::array<unsigned char,0xa00> state{};
    const auto provider_ptr=reinterpret_cast<uintptr_t>(provider.data()),state_ptr=reinterpret_cast<uintptr_t>(state.data());
    memcpy(owner.data()+0x70,&decoder,sizeof(decoder));memcpy(owner.data()+0x78,&provider_ptr,sizeof(provider_ptr));
    memcpy(provider.data()+0x1e8,&state_ptr,sizeof(state_ptr));
    for(unsigned index=0;index<32;++index)state[0x9b8+index]=static_cast<unsigned char>(
        (HexNibble(field[index*2])<<4)|HexNibble(field[index*2+1]));
    assert(PlaybackPumpHook(owner.data(),nullptr,nullptr)==17);
    for(size_t offset=0;offset<32768;offset+=16384) {
        QueueMp3Encoded(decoder,raw.data()+offset,16384,100+.01*offset/16384,epoch);
        Slot slot{};while(Pop(slot))ProcessMp3Capture(slot,buffers);
    }
    ownership::DecodedActivity activity{};activity.owner=decoder;activity.epoch=epoch;
    activity.calls=1;activity.produced_floats=80;activity.first=100.01;activity.last=100.02;tracker.ObserveDecoded(activity);
    Media current;const auto generation=ObserveRecordingMetadata(media,buffers,current,100.1);
    std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
    const auto match_count=SetExpectedPlaybackIdentity(current,generation,matches);
    assert(match_count&&"full native playback field must match metadata without treating its pointer prefix as ID bytes");
    assert(BindDirectReadyCapture(buffers,current,generation,100.1,matches,match_count));
    assert(PlaybackPumpHook(owner.data(),nullptr,nullptr)==17);
    assert(playback_id_payload_offset.load()>=0&&"captured prefix must be resolved from metadata rather than hardcoded");
    PlaybackQualityTracker quality;quality.Media(current.PlaybackKey(),current.title,0,current.duration,100.1);
    const auto snapshot=BoundPlaybackQuality(buffers,current,generation,quality.Snapshot());
    assert(snapshot.decoder_bound&&snapshot.format==PlaybackFormat::Mp3&&snapshot.rate==44100&&snapshot.channels==2);
    PublishPlaybackQuality(snapshot);PlaybackQualitySnapshot client;client.identity=PlaybackIdentity(current.PlaybackKey());
    client.title=snapshot.title;PublishClientPlaybackQuality(client);
    const auto labels=PlaybackQualityLabels(ReadPlaybackQuality());
    assert(labels[2]==L"Format: MP3"&&labels[3]==L"Sample rate: 44,100 Hz");
    CloseHandle(event);event=nullptr;delete[] queue;queue=nullptr;
    std::puts("PASS: real dump bytes + captured native field + recorded metadata resolve ownership and publish MP3/44,100 Hz menu details");
}
std::vector<uint8_t> PodcastFrames(unsigned frames,uint8_t payload=0) {
    std::vector<uint8_t> out={'I','D','3',4,0,0,0,0,0,0};
    for(unsigned frame=0;frame<frames;++frame) {
        const auto at=out.size();out.resize(at+417,payload);
        out[at]=0xff;out[at+1]=0xfb;out[at+2]=0x90;out[at+3]=0;
    }
    return out;
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
    diagnostic_lines.clear();media.position=1.348;
    state.Observe(media,27.258,23,"buffering");
    assert(Contains("timeline-jump")&&Contains("snapshot=buffering")&&Contains("movement_ms=1348")&&
        Contains("elapsed_ms=258")&&Contains("initiator=unknown"));
    InterlockedExchange(&history::log_level_value,1);const auto off=diagnostic_lines.size();
    state.Observe(media,27.5,23);assert(diagnostic_lines.size()==off);
    InterlockedExchange(&history::log_level_value,4);
}
void FlacInputCompletesWithoutDecodedCallbacks() {
    const auto source=recording_fixture::fixture();
    for(bool corrupt:{false,true}) {
        auto bytes=source;if(corrupt)bytes.back()^=1;
        CaptureBuffers buffers;Slot slot{};slot.context=0x7120;slot.epoch=7;
        slot.kind=AudioEventKind::FlacBegin;slot.time=100;ProcessFlacCapture(slot,buffers);
        // Read ahead can supply all compressed frames while playback is skipped
        // before even its first decoded-frame callback.
        slot.kind=AudioEventKind::FlacBytes;
        for(size_t at=0;at<bytes.size();++at) {
            slot.time+=.001;slot.length=1;slot.bytes[0]=bytes[at];ProcessFlacCapture(slot,buffers);
            if(at+1<bytes.size())assert(buffers.ready.empty());
        }
        if(corrupt) {
            assert(buffers.ready.empty()&&"CRC corruption must never become a complete recording");
            continue;
        }
        assert(buffers.active.empty()&&buffers.ready.size()==1&&
               "complete compressed FLAC must not depend on played/decoded samples");
        assert(buffers.ready[0]->coverage.samples==0);
        assert(ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid);
        assert(buffers.ready[0]->data.size()==source.size());
        for(size_t i=0;i<source.size();++i)assert(buffers.ready[0]->data[i]==source[i]);
        slot.kind=AudioEventKind::FlacError;ProcessFlacCapture(slot,buffers);
        assert(buffers.ready.size()==1&&ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid&&
               "a later decoder error cannot revoke complete compressed audio");
    }
    std::puts("PASS: compressed FLAC completion independent of playback callbacks; truncated/CRC-invalid bytes rejected");
}
void FlacRepeatedInputCompletesWithoutDecodedCallbacks() {
    const auto one=recording_fixture::fixture();
    std::vector<uint8_t> source(one.begin(),one.begin()+42);source[25]=48;
    std::array<std::vector<uint8_t>,3> frames;
    for(unsigned i=0;i<frames.size();++i) {
        auto& frame=frames[i];frame.assign(one.begin()+42,one.end());frame[4]=uint8_t(i);
        frame[6]=uint8_t(recording_fixture::crc(std::vector<uint8_t>(frame.begin(),frame.begin()+6),8,7));
        frame.resize(frame.size()-2);const auto crc=recording_fixture::crc(frame,16,0x8005);
        frame.push_back(uint8_t(crc>>8));frame.push_back(uint8_t(crc));
        source.insert(source.end(),frame.begin(),frame.end());
    }
    for(unsigned scenario=0;scenario<3;++scenario) {
        std::vector<uint8_t> input(source.begin(),source.begin()+42);
        for(unsigned index:{0u,1u,0u,1u,2u}) {
            if(scenario==1&&index==1)continue; // Real hole: frame 1 never arrived.
            auto frame=frames[index];
            if(scenario==2&&index==0&&input.size()>42) {
                frame[8]^=1;frame.resize(frame.size()-2);
                const auto crc=recording_fixture::crc(frame,16,0x8005);
                frame.push_back(uint8_t(crc>>8));frame.push_back(uint8_t(crc));
            }
            input.insert(input.end(),frame.begin(),frame.end());
        }
        CaptureBuffers buffers;Slot slot{};slot.context=0x7210+scenario;slot.epoch=7;slot.time=100;
        slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
        diagnostic_lines.clear();
        // Byte-by-byte input also exercises unfinished duplicate headers/CRCs.
        for(const auto byte:input) {
            slot.kind=AudioEventKind::FlacBytes;slot.bytes[0]=byte;slot.length=1;slot.time+=.001;
            ProcessFlacCapture(slot,buffers);
        }
        if(scenario) {
            assert(buffers.ready.empty()&&"gaps and conflicting repeat frames cannot become recordings");
            continue;
        }
        assert(buffers.active.empty()&&buffers.ready.size()==1&&
               "verified repeated compressed frames must recover without PCM callbacks");
        auto& capture=*buffers.ready[0];
        assert(capture.coverage.samples==0&&capture.Samples()==48&&capture.SourceUnits()==3);
        assert(ValidateFinalCapture(capture)==FinalCaptureValidation::Valid);
        assert(capture.data.Flatten()==source&&"recovery must preserve original unique compressed frames exactly");
    }
    std::puts("PASS: FLAC repeated input recovers; real holes and conflicting duplicates remain rejected");
}
void ReplayFlacSource(const wchar_t* path,bool expected_complete,const wchar_t* recovered_path=nullptr) {
    std::ifstream input{std::filesystem::path(path),std::ios::binary};assert(input);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};assert(!bytes.empty());
    CaptureBuffers buffers;Slot slot{};slot.context=0x7120;slot.epoch=7;slot.time=100;
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    for(size_t at=0;at<bytes.size();) {
        slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(std::min(size_t(32768),bytes.size()-at));
        memcpy(slot.bytes,bytes.data()+at,slot.length);slot.time+=.001;
        ProcessFlacCapture(slot,buffers);at+=slot.length;
    }
    assert((buffers.ready.size()==1)==expected_complete);
    if(expected_complete) {
        auto& capture=*buffers.ready[0];
        assert(capture.coverage.samples==0&&capture.flac_source.complete);
        assert(ValidateFinalCapture(capture)==FinalCaptureValidation::Valid);
        auto canonical=capture.data.Flatten();
        if(recovered_path) {
            std::ofstream output{std::filesystem::path(recovered_path),std::ios::binary};assert(output);
            output.write(reinterpret_cast<const char*>(canonical.data()),std::streamsize(canonical.size()));assert(output);
        } else assert(canonical==bytes);
        std::printf("PASS: dump FLAC bytes=%zu samples=%llu compressed_frames=%llu no PCM callbacks\n",bytes.size(),
            static_cast<unsigned long long>(capture.Samples()),static_cast<unsigned long long>(capture.SourceUnits()));
    } else std::puts("PASS: incomplete dump FLAC not marked complete");
}
unsigned ReplayCaptureBuffer(const std::filesystem::path& session,const std::filesystem::path& destination) {
    assert(!stream_dump::Enabled()&&"replay must not write back into its input journal");
    std::ifstream log(session);assert(log);
    std::vector<std::string> events;std::string line;bool closed=false;
    while(std::getline(log,line)) {
        const auto kind=JsonString(line,"kind");
        assert(kind!="capture_gap"&&kind!="write_error"&&"incomplete event evidence cannot be an exact replay");
        if(kind=="capture_event")events.push_back(line);
        if(kind=="session_end") {
            assert(line.find("\"capture_complete\":true")!=std::string::npos);
            closed=true;
        }
    }
    assert(closed&&!events.empty()&&"close Spotify before replaying its exact event journal");
    CaptureBuffers buffers;std::map<std::string,uint64_t> offsets;unsigned complete=0;
    std::filesystem::create_directories(destination);
    const auto export_ready=[&]() {
        for(auto& capture:buffers.ready) {
            if(ValidateFinalCapture(*capture)!=FinalCaptureValidation::Valid)continue;
            auto bytes=capture->data.Flatten();
            std::ofstream output(destination/(L"capture-"+std::to_wstring(++complete)+L".flac"),std::ios::binary);assert(output);
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));assert(output);
        }
    };
    uint64_t previous=0;
    for(const auto& row:events) {
        const auto sequence=JsonUnsigned(row,"sequence");assert(sequence>previous);previous=sequence;
        assert(JsonString(row,"source")=="flac-read-worker");
        Slot slot{};slot.context=uintptr_t(JsonUnsigned(row,"context"));slot.epoch=unsigned(JsonUnsigned(row,"epoch"));
        slot.sequence=JsonUnsigned(row,"producer_sequence");slot.format=unsigned(JsonUnsigned(row,"format"));
        slot.error_source=FlacErrorSource(JsonUnsigned(row,"error_source"));
        slot.requested=JsonUnsigned(row,"requested");slot.returned=JsonUnsigned(row,"returned");
        slot.input_length=JsonUnsigned(row,"input_length");slot.capture=true;
        const std::string time_key="\"started_mono\":";const auto time_at=row.find(time_key);assert(time_at!=std::string::npos);
        slot.time=std::stod(row.substr(time_at+time_key.size()));
        const auto size=JsonUnsigned(row,"bytes");assert(size<=sizeof(slot.bytes));slot.length=unsigned(size);
        const auto file=JsonString(row,"file");const auto relative=std::filesystem::path(file);
        assert(!relative.is_absolute()&&!relative.has_parent_path());
        const auto offset=JsonUnsigned(row,"offset");assert(offset==offsets[file]);offsets[file]+=size;
        if(size) {
            std::ifstream bytes(session.parent_path()/relative,std::ios::binary);assert(bytes);
            bytes.seekg(std::streamoff(offset));bytes.read(reinterpret_cast<char*>(slot.bytes),std::streamsize(size));
            assert(uint64_t(bytes.gcount())==size&&"journal bytes must exist exactly as recorded");
        }
        const auto event=JsonString(row,"event");
        if(event=="capture_reset") {
            export_ready();ResetCaptureBuffers(buffers,nullptr,slot.epoch,slot.time);continue;
        }
        if(event=="capture_discard") {
            const auto found=buffers.active.find(slot.context);
            if(found!=buffers.active.end()&&found->second->capture_epoch==slot.epoch)buffers.active.erase(found);
            continue;
        }
        if(event=="queue_loss"||event=="queue_loss_global") {
            CaptureQueueLossBatch loss;loss.global=event=="queue_loss_global";
            if(!loss.global){loss.count=1;loss.entries[0]={slot.context,slot.epoch,0};}
            ApplyCaptureQueueLosses(buffers,loss,slot.time);continue;
        }
        if(event=="flac_begin")slot.kind=AudioEventKind::FlacBegin;
        else if(event=="flac_bytes")slot.kind=AudioEventKind::FlacBytes;
        else if(event=="flac_frame")slot.kind=AudioEventKind::FlacFrame;
        else if(event=="flac_end")slot.kind=AudioEventKind::FlacEnd;
        else {assert(event=="flac_error");slot.kind=AudioEventKind::FlacError;}
        ProcessFlacCapture(slot,buffers);
    }
    for(const auto& item:offsets)assert(std::filesystem::file_size(session.parent_path()/item.first)==item.second);
    const auto before_export=complete;export_ready();
    std::printf("PASS: exact capture-event replay events=%zu complete=%u unresolved=%zu; no rechunking or synthetic frame callbacks\n",
        events.size(),complete,buffers.active.size()+buffers.ready.size()-(complete-before_export));
    return complete;
}
void CaptureJournalRoundTrip() {
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::filesystem::path(temp)/(L"Floggfy-capture-journal-"+std::to_wstring(GetTickCount64()));
    assert(stream_dump::Start(root.wstring()));const auto folder=std::filesystem::path(stream_dump::SessionFolder());
    const auto source=recording_fixture::fixture();CaptureBuffers buffers;Slot slot{};
    slot.context=0x8180;slot.time=10;slot.kind=AudioEventKind::FlacBegin;slot.sequence=1;ProcessFlacCapture(slot,buffers);
    size_t at=0;
    for(size_t chunk:{size_t(1),size_t(8),source.size()-9}) {
        slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(chunk);slot.input_length=chunk;
        slot.time+=.1;++slot.sequence;memcpy(slot.bytes,source.data()+at,chunk);at+=chunk;ProcessFlacCapture(slot,buffers);
    }
    slot.kind=AudioEventKind::FlacEnd;slot.length=0;++slot.sequence;ProcessFlacCapture(slot,buffers);
    assert(buffers.ready.size()==1);
    slot.context=0x8181;slot.kind=AudioEventKind::FlacBegin;++slot.sequence;ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(source.size()-1);++slot.sequence;
    memcpy(slot.bytes,source.data(),slot.length);ProcessFlacCapture(slot,buffers);
    CaptureQueueLossBatch loss;loss.count=1;loss.entries[0]={slot.context,slot.epoch,0};
    ApplyCaptureQueueLosses(buffers,loss,slot.time);
    // Retirement and settings resets happen outside the parser. A later suffix
    // must not resurrect either discarded prefix during replay.
    for(bool reset:{false,true}) {
        slot.context=0x8182;slot.kind=AudioEventKind::FlacBegin;slot.length=0;++slot.sequence;
        ProcessFlacCapture(slot,buffers);
        slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(source.size()-1);++slot.sequence;
        memcpy(slot.bytes,source.data(),slot.length);ProcessFlacCapture(slot,buffers);
        if(reset)ResetCaptureBuffers(buffers,nullptr,++slot.epoch,slot.time);
        else {TraceCaptureDiscard(*buffers.active.at(slot.context),slot.time);buffers.active.erase(slot.context);}
        slot.kind=AudioEventKind::FlacBytes;slot.length=1;slot.bytes[0]=source.back();++slot.sequence;
        ProcessFlacCapture(slot,buffers);
    }
    assert(stream_dump::Stop());const auto output=root/L"replay";
    assert(ReplayCaptureBuffer(folder/L"session.jsonl",output)==1);
    std::ifstream file(output/L"capture-1.flac",std::ios::binary);
    const std::vector<uint8_t> replayed{std::istreambuf_iterator<char>(file),{}};
    assert(replayed==source&&"exact worker chunks and queue-loss ordering must preserve the same final audio");
    file.close();
    std::filesystem::remove_all(root);
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
    const auto bytes=recording_fixture::fixture();slot.kind=AudioEventKind::FlacBytes;slot.length=unsigned(bytes.size()-1);
    memcpy(slot.bytes,bytes.data(),slot.length);ProcessFlacCapture(slot,buffers);assert(buffers.active.at(slot.context)->metadata);
    uint32_t frame[8]={16,48000,2,0,16,0,0,0};slot.kind=AudioEventKind::FlacFrame;slot.length=32;memcpy(slot.bytes,frame,32);
    diagnostic_lines.clear();const auto prior_failures=failure_events;ProcessFlacCapture(slot,buffers);
    assert(buffers.active.size()==1&&buffers.ready.empty());
    assert(failure_events==prior_failures&&Contains("decoded-coverage-disagrees-with-source"));
    assert(Contains("origin=capture-coverage-guard")&&Contains("reason=format")&&Contains("native_frame_result=0"));
    assert(Contains("expected_rate=44100 got_rate=48000")&&Contains("capture=")&&Contains("queue_lag_ms="));
    // Native errors remain visible even after our capture has already ended.
    slot.kind=AudioEventKind::FlacError;slot.error_source=FlacErrorSource::ReadCallback;slot.format=2;slot.requested=8192;
    diagnostic_lines.clear();ProcessFlacCapture(slot,buffers);assert(Contains("origin=native-read-return")&&Contains("code=2 requested=8192 returned=0"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    slot.kind=AudioEventKind::FlacFrame;memcpy(slot.bytes,frame,32);diagnostic_lines.clear();ProcessFlacCapture(slot,buffers);
    assert(Contains("reason=metadata-not-ready"));
    slot.kind=AudioEventKind::FlacBegin;ProcessFlacCapture(slot,buffers);
    InterlockedExchange(&history::log_level_value,1);diagnostic_lines.clear();
    slot.kind=AudioEventKind::FlacFrame;ProcessFlacCapture(slot,buffers);
    assert(diagnostic_lines.empty()&&buffers.active.size()==1&&
           "native playback coverage does not discard compressed input or emit a default-level failure");
    InterlockedExchange(&history::log_level_value,4);
}
void CaptureReasonLogs() {
    assert(std::strcmp(FlacReaderDetail("complete_recovered"),"all declared source samples verified")==0);
    assert(std::strcmp(FlacReaderDetail("waiting_frame"),"complete frame or declared samples not verified")==0);
    Capture capture;capture.lossless=true;capture.flac.rate=44100;capture.flac.total_samples=88200;
    capture.flac_source.samples=44100;capture.flac_source.frames=1;
    capture.flac_source.reason="resync_limit";capture.bound_identity="Known Artist - Known Track";
    diagnostic_lines.clear();const auto failures=failure_events;
    TraceCaptureDisposition(capture,"released","retention-expired-validation-rejected");
    assert(Contains("Known Artist - Known Track")&&Contains("verified_prefix_ms=1000")&&Contains("expected_ms=2000"));
    assert(Contains("no verified continuation within recovery window")&&failure_events==failures&&
        "incomplete capture logging must explain observed reader failure without claiming user action or song corruption");
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
        assert(rejected.active.size()==1&&rejected.ready.empty()&&
               !rejected.active.begin()->second->flac_source.complete);
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
std::unique_ptr<Capture> CompleteFlacCapture(double born,double finished) {
    auto capture=std::make_unique<Capture>();capture->lossless=true;capture->born=born;capture->finished=finished;
    const auto bytes=recording_fixture::fixture(1,16);
    assert(capture->data.Append(bytes.data(),bytes.size(),bytes.size()+65536));capture->bytes=bytes.size();
    size_t audio=0;assert(ParseFlacMetadata(capture->data,capture->flac,audio)==FlacParse::Valid);
    capture->metadata=true;capture->flac_audio_offset=audio;capture->flac_source.Scan(capture->data,capture->flac,audio);
    capture->coverage.samples=16;capture->coverage.frames=1;return capture;
}
std::unique_ptr<Capture> CompleteOggCapture(double born,double finished) {
    auto capture=std::make_unique<Capture>();capture->born=born;capture->finished=finished;
    const auto bytes=recording_fixture::ogg();
    for(size_t at=0;at<bytes.size();) {
        const size_t header=27+bytes[at+26];size_t length=header;
        for(size_t index=27;index<header;++index)length+=bytes[at+index];
        const auto result=capture->stream.Push(bytes.data()+at,length);
        assert(result!=Result::Invalid&&result!=Result::Ignore);at+=length;
    }
    assert(capture->stream.complete&&capture->data.Append(bytes.data(),bytes.size(),bytes.size()+65536));
    capture->bytes=bytes.size();return capture;
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
    auto decoded_short=CompleteFlacCapture(100,111);decoded_short->coverage.samples=15;
    assert(ValidateFinalCapture(*decoded_short)==FinalCaptureValidation::Valid);
    auto truncated=CompleteFlacCapture(100,111);
    auto short_bytes=truncated->data.Flatten();short_bytes.pop_back();truncated->data={};
    assert(truncated->data.Append(short_bytes.data(),short_bytes.size(),65536));truncated->bytes=short_bytes.size();
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
void CompletedSourceQueueLossCannotRelabel() {
    for(bool lost_boundary:{false,true}) {
        ownership::DecoderOwnershipTracker tracker;tracker.Reset(7);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        auto capture=CompleteFlacCapture(100,101);capture->coverage={};
        capture->capture_context=capture->decoder_owner=0x7120;capture->capture_epoch=7;
        capture->capture_token=tracker.BeginCapture(ownership::DecoderCodec::Flac,0x7120,0,100,7,10);
        tracker.CompleteCapture(capture->capture_token,101,11);
        buffers.ready.push_back(std::move(capture));
        if(lost_boundary) {
            CaptureQueueLossBatch loss;loss.count=1;loss.entries[0].context=0x7120;loss.entries[0].epoch=7;
            ApplyCaptureQueueLosses(buffers,loss,102);
        }
        Media media;media.spotify_uri="spotify:track:next";media.playback_instance="next-native-id";
        media.duration=16;media.title=L"Next song";
        std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
        matches[0].metadata_generation=1;matches[0].decoder=0x7120;matches[0].observed_at=102.1;
        matches[0].role=playback_identity::SnapshotRole::PlayerPlaybackId;
        const auto bound=BindDirectReadyCapture(buffers,media,1,102.1,matches,1);
        assert(bool(bound)==!lost_boundary&&"a lost decoder reuse boundary must prevent relabeling retained source bytes");
        assert(buffers.ready.size()==1&&ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid);
    }
    std::puts("PASS: complete source exact binding before PCM; scoped queue loss preserves bytes without relabeling");
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
    assert(Contains("loss_reports=2")&&"queue-loss log must report the actual number of notifications, not just contexts");
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
           buffers.active[1]->listen_generation==heard.generation&&
           "proven ownership must bypass the capability-fallback timing window");
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
    buffers.ready[0]->exact_identity=true;buffers.ready[0]->association=Heard{};
    AssociateRetainedRecordings(buffers);
    const auto reports=diagnostic_lines.size();
    MarkUnidentifiedInterstitial(buffers,diagnostic.reason);
    assert(buffers.unidentified_interstitial_active&&diagnostic_lines.size()==reports&&
        "an older exact recording cannot clear the live advertisement and repeatedly log it");
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
Media RecordingMedia(const char* instance,double duration=100,bool playing=true) {
    Media media;media.source=MediaSource::Spotify;media.title=L"Recording";
    media.artist=L"Artist";media.album=L"Album";
    media.spotify_uri="spotify:track:0000000000000000000001";
    media.playback_instance=instance;media.duration=duration;media.playing=playing;return media;
}
void StreamOwnedCompletion() {
    {
        CaptureBuffers buffers;const auto source=recording_fixture::ogg();
        std::vector<std::vector<uint8_t>> pages;
        for(size_t at=0;at<source.size();) {
            const auto header=27+source[at+26];size_t size=size_t(header);
            for(size_t lace=27;lace<size_t(header);++lace)size+=source[at+lace];
            pages.emplace_back(source.begin()+at,source.begin()+at+size);at+=size;
        }
        Slot slot{};slot.context=0x7700;slot.kind=AudioEventKind::OggPage;slot.capture=true;
        const auto feed=[&](const std::vector<uint8_t>& page,double time) {
            slot.time=time;slot.length=unsigned(page.size());memcpy(slot.bytes,page.data(),page.size());
            ProcessOggCapture(slot,buffers);
        };
        for(size_t page=0;page+1<pages.size();++page)feed(pages[page],100+page*.01);
        feed(pages.front(),700); // New BOS on the reused context and serial.
        assert(buffers.ogg_active.size()==1&&buffers.ready.size()==1&&!buffers.ready[0]->stream.complete);
        feed(pages.back(),701); // Late EOS belongs uniquely to the predecessor.
        assert(buffers.ready.size()==1&&ValidateFinalCapture(*buffers.ready[0])==FinalCaptureValidation::Valid);
        for(size_t page=1;page<pages.size();++page)feed(pages[page],702+page*.01);
        assert(buffers.ogg_active.empty()&&buffers.ready.size()==2&&
            ValidateFinalCapture(*buffers.ready[1])==FinalCaptureValidation::Valid);
        for(const auto& recording:buffers.ready) {
            assert(recording->data.size()==source.size());
            for(size_t at=0;at<source.size();++at)assert(recording->data[at]==source[at]);
        }
        buffers={};feed(pages.front(),800);feed(pages[2],801);
        assert(buffers.ogg_active.empty()&&buffers.ready.size()==1&&buffers.ready[0]->ambiguous&&
            "an actual page gap stays incomplete and buffered for unmatched handling");
    }
    {
        const auto prior=decoded_sink_target;decoded_sink_target=nullptr;
        CaptureBuffers buffers;Slot slot{};slot.context=0x8000;slot.time=100;
        slot.kind=AudioEventKind::Mp3Begin;ProcessMp3Capture(slot,buffers);
        auto bytes=PodcastFrames(4);std::memcpy(bytes.data()+46,"Xing",4);
        bytes[53]=1;bytes[57]=3;
        slot.kind=AudioEventKind::Mp3Bytes;slot.length=10+417*2;
        std::memcpy(slot.bytes,bytes.data(),slot.length);slot.time=100.1;ProcessMp3Capture(slot,buffers);
        assert(buffers.active.size()==1&&buffers.ready.empty());
        const auto offset=slot.length;slot.length=unsigned(bytes.size()-offset);
        std::memcpy(slot.bytes,bytes.data()+offset,slot.length);slot.time=100.2;ProcessMp3Capture(slot,buffers);
        assert(buffers.active.empty()&&buffers.ready.size()==1&&
            buffers.ready[0]->mp3_info.DeclaredComplete()&&
            "declared MP3 frame coverage completes the final recording without a UI transition");
        decoded_sink_target=prior;
    }
    for(const bool episode:{false,true})for(const auto codec:{Codec::Vorbis,Codec::Flac,Codec::Mp3,Codec::Aac}) {
        ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        auto capture=codec==Codec::Flac?CompleteFlacCapture(100,100.3):CompleteOggCapture(100,100.3);
        auto decoder=ownership::DecoderCodec::Ogg;
        if(codec==Codec::Flac)decoder=ownership::DecoderCodec::Flac;
        if(codec==Codec::Mp3||codec==Codec::Aac) {
            capture=std::make_unique<Capture>();capture->born=100;capture->finished=100.3;
            const auto bytes=codec==Codec::Mp3?PodcastMp3():spac_fixture::stream();
            assert(capture->data.Append(bytes.data(),bytes.size(),1024*1024));capture->bytes=bytes.size();
            if(codec==Codec::Mp3) {
                capture->mp3=true;capture->mp3_source_closed=true;decoder=ownership::DecoderCodec::Mp3;std::string error;
                assert(InspectMp3Source(capture->data,capture->mp3_info,error));
            } else {
                capture=CompleteAacCapture(100,100.3);decoder=ownership::DecoderCodec::Aac;
            }
        }
        capture->retention_start=1;capture->decoder_owner=0x5500;
        capture->capture_token=tracker.BeginCapture(decoder,0x5500,0,100,0,1);
        ownership::DecodedActivity output{};output.owner=0x5500;output.first=100.1;output.last=100.2;
        output.calls=1;output.produced_floats=8192;output.sequence=2;tracker.ObserveDecoded(output);
        tracker.CompleteCapture(capture->capture_token,100.3,3);
        const auto duration=capture->Duration();buffers.ready.push_back(std::move(capture));
        auto next=std::make_unique<Capture>();next->born=100.2;next->retention_start=1;
        buffers.active[0x6600]=std::move(next); // Unrelated prefetched audio.
        auto media=RecordingMedia("source",duration);media.position=.02;
        if(episode)media.spotify_uri="spotify:episode:0000000000000000000001";
        Media current;const auto generation=ObserveRecordingMetadata(media,buffers,current,100.4);
        std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
        matches[0].metadata_generation=generation;matches[0].decoder=0x5500;
        matches[0].role=playback_identity::SnapshotRole::PlayerPlaybackId;
        const auto token=BindDirectReadyCapture(buffers,current,generation,100.4,matches,1);
        assert(token);RememberBoundCaptureIdentity(buffers,current,generation,token);
        // Pauses and controls enrich identity; none can revoke complete bytes.
        media.playing=false;media.explicit_control=media.prior_control=media.explicit_skip=true;
        assert(ObserveRecordingMetadata(media,buffers,current,100.35)==generation);
        ScheduleStreamPublications(buffers,0,100.5);SaveJob job;
        assert(saves.Pop(job)&&!job.unmatched&&job.heard.generation==generation&&
            CachedFinalValidation(*job.capture)==FinalCaptureValidation::Valid&&
            "complete identified audio saves without waiting for playback to reach the end");
        assert(buffers.ready.empty()&&buffers.active.size()==1);
        PlaybackQualitySnapshot fallback;fallback.identity=PlaybackIdentity(current.PlaybackKey());
        assert(BoundPlaybackQuality(buffers,current,generation,fallback).decoder_bound&&
            "saved audio memory is not required to retain menu codec details");
        job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
        assert(!saves.Pop(job));
    }
}
void UnmatchedRetention() {
    const auto prior=history::settings;
    for(const unsigned max_queue:{3u,5u})for(const bool archive:{false,true})for(unsigned state=0;state<5;++state) {
        history::settings.save_unmatched=archive;
        history::settings.matcher_max_queue=max_queue;
        CaptureBuffers buffers;Media current;
        auto first=RecordingMedia("first");ObserveRecordingMetadata(first,buffers,current,100);
        auto capture=CompleteOggCapture(100,101);capture->retention_start=buffers.history.Starts();
        if(state>=1&&state<=3) {
            const auto bytes=recording_fixture::ogg();size_t last=0;
            for(size_t at=0;at<bytes.size();) {
                last=at;const auto header=27+bytes[at+26];size_t length=size_t(header);
                for(size_t lace=27;lace<size_t(header);++lace)length+=bytes[at+lace];
                at+=length;
            }
            assert(capture->data.RetainPrefixAndSuffix(last,bytes.size()));
            capture->bytes=capture->data.size();capture->stream.complete=false;
        } else if(state==4) {
            auto bytes=recording_fixture::ogg();bytes.back()^=1;capture->data={};
            assert(capture->data.Append(bytes.data(),bytes.size(),1024*1024));
        }
        if(state==1)buffers.active[0x5500]=std::move(capture);
        else if(state==2)buffers.ogg_active[{0x5500,42}]=std::move(capture);
        else buffers.ready.push_back(std::move(capture));
        for(unsigned poll=0;poll<20;++poll) {
            first.playing=false;ObserveRecordingMetadata(first,buffers,current,200+poll*100);
            ScheduleStreamPublications(buffers,0,200+poll*100);
        }
        SaveJob job;assert(!saves.Pop(job)&&buffers.history.Starts()==1);
        for(unsigned song=1;song<=max_queue;++song) {
            auto next=RecordingMedia(("repeat-"+std::to_string(song)).c_str());
            ObserveRecordingMetadata(next,buffers,current,3000+song);
            ObserveRecordingMetadata(next,buffers,current,3000+song+.1);
            ScheduleStreamPublications(buffers,0,3000+song+.2);
            assert(buffers.history.Starts()==song+1);
            if(song<max_queue)assert(!saves.Pop(job)&&buffers.ready.size()+buffers.active.size()+buffers.ogg_active.size()==1);
        }
        assert(buffers.ready.empty()&&buffers.active.empty()&&buffers.ogg_active.empty());
        const bool should_save=archive&&state==0;
        assert(saves.Pop(job)==should_save&&"only complete validated unmatched recordings may reach the save worker");
        if(should_save) {
            assert(job.unmatched&&job.correlation.find("\"retired_at_start\":"+std::to_string(max_queue+1))!=std::string::npos);
            job.capture.reset();publishing_reserved.fetch_sub(job.reserved);
        }
    }
    history::settings=prior;
}
void UnmatchedWriterRejectsPartial() {
    const auto prior=history::settings;wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::filesystem::path(temp)/(L"Floggfy-reject-unmatched-"+std::to_wstring(GetTickCount64()));
    history::settings.root=root.wstring();history::settings.save_unmatched=history::settings.downloads=true;
    history::settings.capture_epoch=0;
    for(const bool corrupt:{false,true}) {
        SaveJob job;job.unmatched=true;job.capture=CompleteOggCapture(100,101);
        if(corrupt) {
            auto bytes=recording_fixture::ogg();bytes.back()^=1;job.capture->data={};
            assert(job.capture->data.Append(bytes.data(),bytes.size(),1024*1024));
        } else job.capture->stream.complete=false;
        assert(PublishUnmatched(job)==Publication::Blocked&&
            "the writer must independently refuse incomplete or invalid unmatched recordings");
        assert(!std::filesystem::exists(root)&&"rejected unmatched audio must not create audio or sidecar files");
    }
    history::settings=prior;
}
void RecordingQueueCounters() {
    const auto before=recording_counters.Snapshot();CaptureBuffers buffers;Media current;
    const auto generation=ObserveRecordingMetadata(RecordingMedia("counter-one",1),buffers,current,100);
    ObserveRecordingMetadata(RecordingMedia("counter-one",1),buffers,current,100.1);
    assert(recording_counters.Snapshot().metadata_push==before.metadata_push+1);
    auto recording=CompleteOggCapture(100,101);recording->stream_counter.Begin(RecordKind::Stream);
    recording->association=buffers.history.Find(generation)->metadata;
    for(unsigned fill=0;fill<2;++fill) {
        auto filler=CompleteOggCapture(200,201);assert(QueueRecording(filler,0,true,"{}\n"));
    }
    const auto pending=recording_counters.Snapshot();
    assert(!QueueRecording(recording,0,false,{},&buffers.history)&&recording);
    assert(buffers.history.Find(generation)->metadata_counter.Active());
    assert(recording_counters.Snapshot()==pending&&"queue-full retry cannot pop or duplicate either record");
    SaveJob job;while(saves.Pop(job)){job.capture.reset();publishing_reserved.fetch_sub(job.reserved);}
    assert(QueueRecording(recording,0,false,{},&buffers.history)&&!recording);
    assert(!buffers.history.Find(generation)->metadata_counter.Active());
    assert(saves.Pop(job)&&job.metadata_counter.Active()&&job.capture->stream_counter.Active());
    const auto queued=recording_counters.Snapshot();
    assert(queued.stream_pop==before.stream_pop&&queued.metadata_pop==before.metadata_pop&&
        "joined records remain owned while their disk save is pending");
    job.capture.reset();job.metadata_counter.Reset();publishing_reserved.fetch_sub(job.reserved);
    const auto released=recording_counters.Snapshot();
    assert(released.stream_push==before.stream_push+1&&released.stream_pop==before.stream_pop+1&&
        released.metadata_push==before.metadata_push+1&&released.metadata_pop==before.metadata_pop+1);
}
void UnmatchedArchiveRetry(bool fail_node=false) {
    const auto prior=history::settings;
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::filesystem::path(temp)/(L"Floggfy-unmatched-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    history::settings.root=root.wstring();history::settings.save_unmatched=true;
    history::settings.capture_epoch=0;history::settings.downloads=true;
    {std::ofstream blocked(root/L"Unmatched");blocked<<"block directory creation";}
    auto capture=CompleteOggCapture(100,101);
    assert(QueueRecording(capture,0,true,"{\"reason\":\"test unmatched\"}\n"));
    const auto failures=retry_node_failures.load();fail_retry_node=fail_node;
    const auto prior_event=save_event;save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(save_event);
    const auto prior_workers=workers_running.exchange(true);
    const auto worker=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);assert(worker);
    if(fail_node) {
        const auto node_deadline=GetTickCount64()+5000;
        while(retry_node_failures.load()==failures&&GetTickCount64()<node_deadline)Sleep(10);
        assert(retry_node_failures.load()==failures+1);
    } else Sleep(150);
    assert(publishing_reserved.load()>=recording_fixture::ogg().size()&&"failed archive publication must keep the recording in RAM");
    std::filesystem::remove(root/L"Unmatched");
    const auto deadline=GetTickCount64()+5000;
    while(publishing_reserved.load()&&GetTickCount64()<deadline)Sleep(20);
    workers_running=false;SetEvent(save_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CloseHandle(worker);CloseHandle(save_event);save_event=prior_event;workers_running=prior_workers;
    assert(publishing_reserved.load()==0&&"successful disk publication releases its audio reservation");
    assert(retry_node_failures.load()==failures+(fail_node?1:0)&&!fail_retry_node.load());
    unsigned audio=0,sidecars=0,commits=0;
    for(const auto& file:std::filesystem::directory_iterator(root/L"Unmatched")) {
        if(file.path().extension()==L".ogg") {
            ++audio;std::ifstream input(file.path(),std::ios::binary);
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
            assert(bytes==recording_fixture::ogg()&&"archival preserves the exact original Ogg pages");
        } else if(file.path().extension()==L".json")++sidecars;
        else if(file.path().extension()==L".commit")++commits;
        else assert(false&&"no partial publication should remain after successful archive");
    }
    assert(audio==1&&sidecars==1&&commits==1);
    std::filesystem::remove_all(root);history::settings=prior;
}
void PrefetchCannotSupplyCurrentIdentity() {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    auto current=std::make_unique<Capture>();current->born=100;current->decoder_owner=0x8800;
    current->stream.pages=1;current->stream.rate=44100;current->stream.channels=2;
    current->capture_token=tracker.BeginCapture(ownership::DecoderCodec::Ogg,0x8800,0,100,0,1);
    ownership::DecodedActivity output{};output.owner=0x8800;output.first=output.last=100.1;
    output.calls=1;output.produced_floats=2048;output.sequence=2;tracker.ObserveDecoded(output);
    buffers.active[0x8800]=std::move(current);
    auto prefetched=CompleteOggCapture(100.2,100.4);prefetched->decoder_owner=0x9900;
    prefetched->capture_token=tracker.BeginCapture(ownership::DecoderCodec::Ogg,0x9900,0,100.2,0,3);
    output.owner=0x9900;output.first=output.last=100.3;output.sequence=4;tracker.ObserveDecoded(output);
    tracker.CompleteCapture(prefetched->capture_token,100.4,5);buffers.ready.push_back(std::move(prefetched));
    Media snapshot;ObserveRecordingMetadata(RecordingMedia("current",1),buffers,snapshot,100.5);
    AssociateRetainedRecordings(buffers);
    assert(!buffers.ready[0]->association&&!buffers.active[0x8800]->association&&
        "unknown current-stream duration cannot make the prefetched source a unique identity match");
}
void MatcherToleranceAndLogs() {
    const auto prior=history::settings;const auto pump=playback_pump_target;playback_pump_target=nullptr;
    for(unsigned scenario=0;scenario<4;++scenario) {
        history::settings.matcher_time_offset_ms=scenario==1?1000:scenario==2?0:10000;
        history::settings.matcher_duration_ms=scenario==3?100:250;
        CaptureBuffers buffers;auto recording=CompleteOggCapture(100,100.3);recording->retention_start=1;
        buffers.ready.push_back(std::move(recording));Media current;
        diagnostic_lines.clear();
        ObserveRecordingMetadata(RecordingMedia("delayed-metadata",1.2),buffers,current,109);
        assert(!buffers.ready[0]->association&&"metadata collection never performs the join");
        AssociateRetainedRecordings(buffers);
        assert(bool(buffers.ready[0]->association)==(scenario==0));
        if(scenario==0)assert(Contains("matched-unique-evidence")&&Contains("duration_delta_ms=-200.0")&&Contains("end_delta_ms="));
        if(scenario==1)assert(Contains("outside-start-window"));
        if(scenario==3)assert(Contains("duration-mismatch"));
    }
    history::settings=prior;playback_pump_target=pump;
}
void RetirementCannotManufactureMatch() {
    const auto prior=history::settings;const auto pump=playback_pump_target;playback_pump_target=nullptr;
    history::settings.matcher_max_queue=5;
    for(bool archive:{false,true})for(bool incomplete:{false,true})for(unsigned collection=0;collection<3;++collection) {
        history::settings.save_unmatched=archive;
        CaptureBuffers buffers;Media current;
        ObserveRecordingMetadata(RecordingMedia("unrelated-first",10),buffers,current,100);
        auto first=CompleteOggCapture(100,100.3);first->capture_token=101;first->retention_start=1;
        if(incomplete)first->stream.complete=false;
        auto* first_pointer=first.get();
        if(collection==1)buffers.active[0x5500]=std::move(first);
        else if(collection==2)buffers.ogg_active[{0x5500,42}]=std::move(first);
        else buffers.ready.push_back(std::move(first));
        const auto b=ObserveRecordingMetadata(RecordingMedia("candidate-b",1),buffers,current,100.2);
        auto second=CompleteOggCapture(100.2,100.5);second->capture_token=102;second->retention_start=2;
        buffers.ready.push_back(std::move(second));
        AssociateRetainedRecordings(buffers);
        assert(!first_pointer->association&&!buffers.ready.back()->association);
        if(incomplete) {
            first_pointer->ambiguous=true;AssociateRetainedRecordings(buffers);
            assert(!buffers.ready.back()->association&&"decoder rejection cannot eliminate competing identity evidence");
        }
        for(unsigned start=3;start<=6;++start) {
            ObserveRecordingMetadata(RecordingMedia(("later-"+std::to_string(start)).c_str(),20),buffers,current,200+start);
            ScheduleStreamPublications(buffers,0,200+start);
        }
        SaveJob job;
        while(saves.Pop(job)){assert(job.unmatched);job.capture.reset();publishing_reserved.fetch_sub(job.reserved);}
        assert(buffers.ready.size()==1&&buffers.history.Find(b));
        AssociateRetainedRecordings(buffers);
        assert(!buffers.ready[0]->association&&"removing an ambiguous competitor cannot create matching evidence");
    }
    history::settings=prior;playback_pump_target=pump;
}
void RetiredMetadataCannotManufactureMatch() {
    const auto prior=history::settings;const auto pump=playback_pump_target;playback_pump_target=nullptr;
    history::settings.matcher_max_queue=5;history::settings.save_unmatched=false;
    CaptureBuffers buffers;Media current;
    ObserveRecordingMetadata(RecordingMedia("candidate-a",1),buffers,current,100);
    const auto b=ObserveRecordingMetadata(RecordingMedia("candidate-b",1),buffers,current,100.2);
    auto capture=CompleteOggCapture(100.1,101);capture->capture_token=601;capture->retention_start=2;
    buffers.ready.push_back(std::move(capture));AssociateRetainedRecordings(buffers);
    assert(!buffers.ready[0]->association);
    for(unsigned start=3;start<=6;++start) {
        ObserveRecordingMetadata(RecordingMedia(("later-meta-"+std::to_string(start)).c_str(),20),buffers,current,200+start);
        ScheduleStreamPublications(buffers,0,200+start);
    }
    assert(buffers.ready.size()==1&&buffers.history.Find(b));AssociateRetainedRecordings(buffers);
    assert(!buffers.ready[0]->association&&"retiring ambiguous metadata cannot establish identity");
    RetiredMatchEvidence evidence;evidence.RememberCapture({1,100,1});
    for(unsigned i=0;i<32;++i)evidence.RememberCapture({i+2,1000.0+i,2});
    assert(evidence.BlocksMetadata(100,2,10000,250)&&"overflow must preserve unknown competitor coverage");
    assert(!evidence.BlocksMetadata(120,2,10000,250)&&"overflow must not poison provably later windows");
    history::settings=prior;playback_pump_target=pump;
}
void MatcherSnapshotDeltas() {
    const auto pump=playback_pump_target;playback_pump_target=nullptr;
    CaptureBuffers buffers;Media current;
    ObserveRecordingMetadata(RecordingMedia("far-away",1),buffers,current,500);
    auto capture=CompleteOggCapture(100,101);capture->capture_token=800;
    buffers.ready.push_back(std::move(capture));diagnostic_lines.clear();
    AssociateRetainedRecordings(buffers);
    assert(Contains("matcher_candidate")&&Contains("outside-start-window"));
    const auto full_lines=diagnostic_lines.size();
    diagnostic_lines.clear();
    auto competitor=CompleteOggCapture(100,101);competitor->capture_token=801;
    buffers.ready.push_back(std::move(competitor));
    AssociateRetainedRecordings(buffers);
    std::vector<std::string> changed;
    for(const auto& line:diagnostic_lines)if(line.find(" capture=800 ")!=std::string::npos)changed.push_back(line);
    assert(changed.size()<full_lines&&"a competitor change must not resend unchanged candidate rows");
    assert(std::none_of(changed.begin(),changed.end(),[](const auto& line){return line.find("matcher_candidate ")!=std::string::npos;}));
    assert(Contains("mode=delta")&&Contains("base_snapshot=")&&Contains("competitor=801"));
    diagnostic_lines.clear();buffers.ready.pop_back();AssociateRetainedRecordings(buffers);
    assert(Contains("matcher_competitor_removed")&&Contains("competitor=801"));
    diagnostic_lines.clear();EmitMatcherSnapshot(*buffers.ready[0],MatcherReason::RetiredInvalid);
    assert(Contains("mode=full")&&Contains("matcher_terminal reason=retired-invalid")&&
        Contains("matcher_candidate")&&Contains("outside-start-window"));
    playback_pump_target=pump;
}
void MatcherCandidateInventory() {
    MatcherSnapshotDeltas();
    const auto prior=history::settings;const auto pump=playback_pump_target;playback_pump_target=nullptr;
    CaptureBuffers buffers;Media current;
    const auto assigned=ObserveRecordingMetadata(RecordingMedia("assigned",1),buffers,current,100);
    buffers.history.MarkAssigned(assigned);
    auto unusable=RecordingMedia("unusable",1);unusable.title.clear();
    ObserveRecordingMetadata(unusable,buffers,current,101);
    ObserveRecordingMetadata(RecordingMedia("far-away",1),buffers,current,500);
    auto capture=CompleteOggCapture(100,101);capture->capture_token=501;capture->retention_start=3;
    buffers.ready.push_back(std::move(capture));diagnostic_lines.clear();
    AssociateRetainedRecordings(buffers);
    assert(Contains("matcher_snapshot")&&Contains("metadata_total=3")&&Contains("metadata-assigned")&&
        Contains("metadata-unusable")&&Contains("outside-start-window")&&
        "inventory must include every retained candidate before production filters");
    const auto once=diagnostic_lines.size();AssociateRetainedRecordings(buffers);
    assert(diagnostic_lines.size()==once&&"unchanged polls must not repeat snapshots");
    auto& retained=*buffers.ready[0];
    retained.bytes+=16384;
    ObserveRecordingMetadata(RecordingMedia("far-away",1),buffers,current,501);
    AssociateRetainedRecordings(buffers);
    assert(diagnostic_lines.size()==once&&"byte progress and later metadata observation alone must not repeat snapshots");
    retained.matcher_snapshot.end_ms=102000;
    EmitMatcherSnapshot(retained);
    assert(diagnostic_lines.size()==once&&"absolute diagnostic timestamp alone must not repeat snapshots");
    retained.matcher_snapshot.metadata[2].start_delta_ms=250;
    EmitMatcherSnapshot(retained);
    assert(diagnostic_lines.size()>once&&Contains("start_delta_ms=250")&&
        "changed relative timing evidence must remain visible");
    const auto changed_timing=diagnostic_lines.size();
    retained.matcher_snapshot.metadata[2].decision=MatcherReason::DurationMismatch;
    EmitMatcherSnapshot(retained);
    assert(diagnostic_lines.size()>changed_timing&&Contains("decision=duration-mismatch"));
    const auto changed_reason=diagnostic_lines.size();
    retained.matcher_snapshot.duration_tolerance_ms+=1;
    EmitMatcherSnapshot(retained);
    assert(diagnostic_lines.size()>changed_reason&&"settings changes must remain visible");
    const auto changed_settings=diagnostic_lines.size();
    EmitMatcherSnapshot(retained,MatcherReason::CaptureAborted);
    assert(diagnostic_lines.size()>changed_settings&&Contains("matcher_terminal reason=capture-aborted")&&
        "terminal lifecycle evidence must bypass unchanged-snapshot suppression");
    history::settings.save_unmatched=false;history::settings.matcher_max_queue=1;
    ObserveRecordingMetadata(RecordingMedia("retire",20),buffers,current,600);
    ScheduleStreamPublications(buffers,0,601);
    assert(buffers.ready.empty()&&Contains("matcher_terminal")&&Contains("save-unmatched-disabled"));
    for(const auto& line:diagnostic_lines)
        assert(line.size()<768&&line.find("spotify:track:")==std::string::npos&&line.find("far-away")==std::string::npos);
    history::settings=prior;playback_pump_target=pump;
}
void LateDurationContradictionStaysRejected() {
    const auto prior=history::settings;const auto pump=playback_pump_target;playback_pump_target=nullptr;
    history::settings.matcher_duration_ms=250;history::settings.matcher_max_queue=5;
    CaptureBuffers buffers;Media current;auto media=RecordingMedia("late-duration",1.2);
    ObserveRecordingMetadata(media,buffers,current,100);
    auto capture=CompleteOggCapture(100,101);capture->capture_token=701;capture->retention_start=1;
    buffers.ready.push_back(std::move(capture));AssociateRetainedRecordings(buffers);
    assert(buffers.ready[0]->association);
    media.duration=2;ObserveRecordingMetadata(media,buffers,current,101);
    diagnostic_lines.clear();ScheduleStreamPublications(buffers,0,102);SaveJob job;
    assert(!saves.Pop(job)&&buffers.ready.size()==1&&buffers.ready[0]->fallback_ambiguous&&
        "late duration evidence must invalidate a weak association before publication");
    assert(Contains("decision=duration-mismatch")&&Contains("duration_delta_ms=-1000"));
    const auto first=diagnostic_lines.size();ScheduleStreamPublications(buffers,0,103);
    assert(diagnostic_lines.size()==first&&"a rejected weak association cannot oscillate back to matched in diagnostics");
    history::settings=prior;playback_pump_target=pump;
}
void NativeIdentityUsesRecordingLifetime() {
    const auto prior_sink=decoded_sink_target,prior_pump=playback_pump_target;
    const auto prior_layout=decoder_ownership_layout;const auto prior_available=decoder_ownership_layout_available;
    const auto prior_offset=playback_id_payload_offset.load();
    decoded_sink_target=reinterpret_cast<void*>(0x1000);playback_pump_target=reinterpret_cast<void*>(0x2000);
    decoder_ownership_layout={};decoder_ownership_layout.ogg_capture_to_decoder=-136;decoder_ownership_layout_available=true;
    const char* a="00112233445566778899aabbccddeeff";
    const char* b="102132435465768798a9bacbdcedfe0f";
    const auto observe=[&](const char* id,double time,uint64_t sequence) {
        std::array<uint8_t,32> field{};std::fill(field.begin(),field.end(),0x77);
        for(unsigned i=0;i<16;++i)field[8+i]=uint8_t((HexNibble(id[i*2])<<4)|HexNibble(id[i*2+1]));
        playback_identity_bridge.Observe(0x9000,0x8800,0,sequence,field.data(),field.size(),
            playback_identity::SnapshotRole::PlayerPlaybackId,time);
        assert(playback_identity_bridge.TakeObservation().has_value());
    };
    for(const bool old_exists:{false,true}) {
        ResetPlaybackIdentityBridge();ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
        if(old_exists) {
            auto old=CompleteOggCapture(100,100.3);
            BeginCaptureOwnership(*old,buffers,ownership::DecoderCodec::Ogg,0x8888,42,100,0,1);
            ownership::DecodedActivity output{};output.owner=0x8800;output.first=output.last=100.2;
            output.calls=1;output.produced_floats=2048;output.sequence=2;tracker.ObserveDecoded(output);
            tracker.CompleteCapture(old->capture_token,100.3,3);buffers.ready.push_back(std::move(old));
        }
        observe(a,100.1,1);
        auto newer=CompleteOggCapture(200,200.3);
        BeginCaptureOwnership(*newer,buffers,ownership::DecoderCodec::Ogg,0x8888,42,200,0,4);
        ownership::DecodedActivity output{};output.owner=0x8800;output.first=output.last=200.2;
        output.calls=1;output.produced_floats=2048;output.sequence=5;tracker.ObserveDecoded(output);
        tracker.CompleteCapture(newer->capture_token,200.3,6);buffers.ready.push_back(std::move(newer));
        buffers.retired_evidence.RememberCapture({999,200,1});
        buffers.retired_evidence.RememberMetadata({999,200,1});
        Media current;ObserveRecordingMetadata(RecordingMedia(a,1),buffers,current,300);
        AssociateRetainedRecordings(buffers);
        assert(!buffers.ready.back()->association&&"a stale decoder-address observation cannot label a later source");
        if(old_exists)assert(buffers.ready.front()->association&&"older source identity remains usable for late metadata");
        observe(b,200.1,2);ObserveRecordingMetadata(RecordingMedia(b,1),buffers,current,301);
        AssociateRetainedRecordings(buffers);
        assert(buffers.ready.back()->association&&buffers.ready.back()->association->media.playback_instance==b);
    }
    // A native ID can precede a cold source's first bytes. Late GUI metadata
    // must still bind that ongoing source after its native field is refreshed.
    {
        ResetPlaybackIdentityBridge();ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;Media current;
        observe(a,199.9,1);
        auto ongoing=std::make_unique<Capture>();ongoing->born=200;
        ongoing->stream.rate=44100;ongoing->stream.channels=2;ongoing->stream.active=true;
        BeginCaptureOwnership(*ongoing,buffers,ownership::DecoderCodec::Ogg,0x8888,42,200,0,4);
        ownership::DecodedActivity output{};output.owner=0x8800;output.first=output.last=200.2;
        output.calls=1;output.produced_floats=2048;output.sequence=5;tracker.ObserveDecoded(output);
        buffers.ogg_active[{0x8888,42}]=std::move(ongoing);
        ObserveRecordingMetadata(RecordingMedia(a,1),buffers,current,201);
        std::array<uint8_t,32> field{};std::fill(field.begin(),field.end(),0x77);
        for(unsigned i=0;i<16;++i)field[8+i]=uint8_t((HexNibble(a[i*2])<<4)|HexNibble(a[i*2+1]));
        playback_identity_bridge.Observe(0x9000,0x8800,0,2,field.data(),field.size(),
            playback_identity::SnapshotRole::PlayerPlaybackId,201.1,200);
        AssociateRetainedRecordings(buffers);
        assert(buffers.ogg_active.begin()->second->association&&
            "cold-source native ID plus delayed GUI metadata can bind before EOS");
    }
    // Refreshing an inherited ID after a new source begins does not make it
    // evidence for that new source. A had metadata but no retained recording.
    for(const bool same_duration:{false,true}) {
        ResetPlaybackIdentityBridge();ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
        CaptureBuffers buffers;buffers.ownership_tracker=&tracker;Media current;
        ObserveRecordingMetadata(RecordingMedia(a,same_duration?1:5),buffers,current,100);
        observe(a,100.1,1);
        auto newer=CompleteOggCapture(200,200.3);
        BeginCaptureOwnership(*newer,buffers,ownership::DecoderCodec::Ogg,0x8888,42,200,0,4);
        ownership::DecodedActivity output{};output.owner=0x8800;output.first=output.last=200.2;
        output.calls=1;output.produced_floats=2048;output.sequence=5;tracker.ObserveDecoded(output);
        tracker.CompleteCapture(newer->capture_token,200.3,6);buffers.ready.push_back(std::move(newer));
        std::array<uint8_t,32> inherited{};std::fill(inherited.begin(),inherited.end(),0x77);
        for(unsigned i=0;i<16;++i)inherited[8+i]=uint8_t((HexNibble(a[i*2])<<4)|HexNibble(a[i*2+1]));
        playback_identity_bridge.Observe(0x9000,0x8800,0,2,inherited.data(),inherited.size(),
            playback_identity::SnapshotRole::PlayerPlaybackId,201,200);
        AssociateRetainedRecordings(buffers);
        assert(bool(buffers.ready.back()->association)==same_duration&&
            "a freshly observed unchanged inherited ID requires complete source-duration corroboration");
        if(!same_duration) {
            observe(b,201.1,3);ObserveRecordingMetadata(RecordingMedia(b,1),buffers,current,202);
            AssociateRetainedRecordings(buffers);
            assert(buffers.ready.back()->association&&buffers.ready.back()->association->media.playback_instance==b&&
                "the changed native ID can identify the new source after an inherited ID was deferred");
        }
    }
    ResetPlaybackIdentityBridge();ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;Media current;
    ObserveRecordingMetadata(RecordingMedia(a,1,false),buffers,current,100);
    ObserveRecordingMetadata(RecordingMedia(b,1,false),buffers,current,101);
    const auto generation=ObserveRecordingMetadata(RecordingMedia(a,1,true),buffers,current,102);
    observe(a,102.1,1);AssociateRetainedRecordings(buffers);
    assert(buffers.history.Current()->metadata.generation==generation&&playback_id_payload_offset.load()==8&&
        "resuming an existing record must learn the current native layout, not the last inserted identity");
    ResetPlaybackIdentityBridge();playback_id_payload_offset=prior_offset;
    decoded_sink_target=prior_sink;playback_pump_target=prior_pump;
    decoder_ownership_layout=prior_layout;decoder_ownership_layout_available=prior_available;
}
void RestartedFlacKeepsMetadataClaim() {
    const auto old_sink=decoded_sink_target;const auto old_layout=decoder_ownership_layout;
    const auto old_available=decoder_ownership_layout_available;
    decoded_sink_target=reinterpret_cast<void*>(0x1000);
    decoder_ownership_layout={};decoder_ownership_layout_available=true;
    auto source=recording_fixture::fixture();source[25]=32;
    const std::vector<uint8_t> first(source.begin()+42,source.end());auto second=first;
    second[4]=1;second[6]=uint8_t(recording_fixture::crc(std::vector<uint8_t>(second.begin(),second.begin()+6),8,7));
    second.resize(second.size()-2);const auto crc=recording_fixture::crc(second,16,0x8005);
    second.push_back(uint8_t(crc>>8));second.push_back(uint8_t(crc));
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(0);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    Media current;auto media=RecordingMedia("restart-native-id",32.0/44100);
    const auto generation=ObserveRecordingMetadata(media,buffers,current,100);
    Slot slot{};slot.context=0x7650;slot.time=100.01;slot.sequence=1;slot.kind=AudioEventKind::FlacBegin;
    ProcessFlacCapture(slot,buffers);
    const auto send=[&](const std::vector<uint8_t>& bytes) {
        slot.kind=AudioEventKind::FlacBytes;slot.time+=.01;++slot.sequence;slot.length=unsigned(bytes.size());
        memcpy(slot.bytes,bytes.data(),bytes.size());ProcessFlacCapture(slot,buffers);
    };
    send(source);const auto original=buffers.active.at(slot.context)->capture_token;
    ownership::DecodedActivity output{};output.owner=slot.context;output.calls=1;output.produced_floats=32;
    output.first=output.last=slot.time;output.sequence=slot.sequence;tracker.ObserveDecoded(output);
    std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
    matches[0].metadata_generation=generation;matches[0].decoder=slot.context;
    matches[0].role=playback_identity::SnapshotRole::PlayerPlaybackId;
    auto bind=[&]() {
        matches[0].observed_at=slot.time;
        const auto token=BindDirectReadyCapture(buffers,current,generation,slot.time,matches,1);
        if(token)RememberBoundCaptureIdentity(buffers,current,generation,token);
        return token;
    };
    const auto playback=bind();
    assert(playback&&tracker.Binding(original).playback_token==playback&&buffers.history.Find(generation)->assigned);
    uint32_t frame[8]={16,44100,2,0,16,0,0,0};
    slot.kind=AudioEventKind::FlacFrame;slot.length=32;memcpy(slot.bytes,frame,32);ProcessFlacCapture(slot,buffers);
    for(unsigned retry=0;retry<9;++retry) {
        send(first);slot.kind=AudioEventKind::FlacFrame;slot.length=32;++slot.sequence;
        memcpy(slot.bytes,frame,32);ProcessFlacCapture(slot,buffers);
    }
    const auto replacement=buffers.active.at(slot.context)->capture_token;
    assert(replacement!=original&&buffers.history.Find(generation)->assigned);
    send(second);assert(buffers.ready.size()==1);
    const auto rebound=bind();
    if(!rebound)for(const auto& line:diagnostic_lines)
        if(line.find("native_match")!=std::string::npos)std::fprintf(stderr,"%s\n",line.c_str());
    assert(rebound==playback&&tracker.Binding(replacement).state==ownership::OwnershipState::Bound&&
        tracker.Binding(replacement).playback_token==playback&&
        "a verified same-source restart must replace its abandoned incomplete metadata owner");
    assert(buffers.ready[0]->association&&buffers.ready[0]->association->generation==generation);
    ScheduleStreamPublications(buffers,0,slot.time);SaveJob job;
    assert(saves.Pop(job)&&!job.unmatched&&job.heard.generation==generation&&buffers.ready.empty());
    job.capture.reset();job.metadata_counter.Reset();publishing_reserved.fetch_sub(job.reserved);
    decoded_sink_target=old_sink;decoder_ownership_layout=old_layout;decoder_ownership_layout_available=old_available;
    std::puts("PASS: repeated FLAC restarts retain the exact metadata claim and queue one named recording");
}
void ReleaseAuditHonorsBudget() {
    const auto prior=history::settings;const auto prior_pending=publishing_reserved.load(),prior_active=capture_reserved.load();
    history::settings.max_buffered_mib=8;publishing_reserved.store(8u*1024*1024);capture_reserved=0;
    auto capture=CompleteOggCapture(100,101);const auto bytes=capture->data.size();diagnostic_lines.clear();
    CheckBeforeRelease(*capture,"construction budget denied");
    assert(capture->data.size()==bytes&&Contains("validation unavailable")&&
        "an optional deep release audit must not allocate a whole-source copy over budget");
    publishing_reserved.store(prior_pending);capture_reserved.store(prior_active);history::settings=prior;
}
void Mp3BoundaryDoesNotProveCompleteness() {
    CaptureBuffers buffers;auto capture=std::make_unique<Capture>();
    const auto prefix=PodcastFrames(383);capture->mp3=true;capture->born=100;capture->last_input=110;
    capture->bytes=prefix.size();capture->retention_start=1;capture->mp3_bound_duration=100;
    capture->association=Heard{RecordingMedia("skipped-long-mp3",100),100,110,1};capture->exact_identity=true;
    assert(capture->data.Append(prefix.data(),prefix.size(),1024*1024));buffers.active[0x8000]=std::move(capture);
    assert(CompleteMp3Capture(buffers.active.begin(),buffers,110));
    ScheduleStreamPublications(buffers,0,110);SaveJob job;
    assert(!saves.Pop(job)&&buffers.ready.size()==1&&
        "a frame-aligned ten-second source prefix cannot save as a complete hundred-second recording");
}
void IndependentSaveProgress() {
    const auto prior=history::settings;wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto root=std::filesystem::path(temp)/(L"Floggfy-save-progress-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);history::settings.root=root.wstring();
    history::settings.save_unmatched=history::settings.downloads=history::settings.ogg=true;
    history::settings.capture_epoch=0;
    {std::ofstream block(root/L"Unmatched");block<<"blocked";}
    for(unsigned blocked=0;blocked<2;++blocked) {
        auto first=CompleteOggCapture(100,101);assert(QueueRecording(first,0,true,"{\"reason\":\"test\"}\n"));
    }
    auto media=RecordingMedia("writable",1);media.title=L"Writable";
    const auto path=OutputPath(root.wstring(),CatalogFor(media),L".ogg",false);
    const auto prior_event=save_event;save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    const auto prior_workers=workers_running.exchange(true);
    const auto worker=CreateThread(nullptr,0,SaveWorker,nullptr,0,nullptr);assert(worker);
    Sleep(150);
    auto second=CompleteOggCapture(102,103);second->association=Heard{media,102,103,2};
    assert(QueueRecording(second,0,false));
    const auto deadline=GetTickCount64()+2500;
    while(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES&&GetTickCount64()<deadline)Sleep(20);
    const bool progressed=GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
    workers_running=false;SetEvent(save_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CloseHandle(worker);CloseHandle(save_event);save_event=prior_event;workers_running=prior_workers;
    assert(progressed&&"two unwritable archives cannot block the next identified recording");
    assert(publishing_reserved.load()==0);
    std::filesystem::remove_all(root);history::settings=prior;
}
void PublicationLogging() {
    assert(PublicationLogKind(Publication::Saved,true)==LogKind::Finished);
    assert(PublicationLogKind(Publication::Saved,false)==LogKind::Activity);
    assert(PublicationLogKind(Publication::AlreadyPresent,false)==LogKind::Activity);
    assert(PublicationLogKind(Publication::RetryIO,false)==LogKind::Failure);
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
    InterlockedExchange(&history::log_level_value,4);
    if(argc==2&&!wcscmp(argv[1],L"--flac-source-completion")){FlacInputCompletesWithoutDecodedCallbacks();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--flac-repeat-recovery")){FlacRepeatedInputCompletesWithoutDecodedCallbacks();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--flac-source-loss")){CompletedSourceQueueLossCannotRelabel();return 0;}
    if(argc==4&&!wcscmp(argv[1],L"--flac-source-file")){ReplayFlacSource(argv[2],!wcscmp(argv[3],L"complete"));return 0;}
    if(argc==4&&!wcscmp(argv[1],L"--flac-recover-file")){ReplayFlacSource(argv[2],true,argv[3]);return 0;}
    if(argc==4&&!wcscmp(argv[1],L"--capture-buffer-replay")){ReplayCaptureBuffer(argv[2],argv[3]);return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--capture-journal")){CaptureJournalRoundTrip();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--unmatched-complete-only")){UnmatchedRetention();UnmatchedWriterRejectsPartial();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-prefetch")){PrefetchCannotSupplyCurrentIdentity();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-retirement")){RetirementCannotManufactureMatch();RetiredMetadataCannotManufactureMatch();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-matcher-inventory")){MatcherCandidateInventory();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-late-duration")){LateDurationContradictionStaysRejected();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-audit-budget")){ReleaseAuditHonorsBudget();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-retry-allocation")){UnmatchedArchiveRetry(true);return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-mp3-prefix")){Mp3BoundaryDoesNotProveCompleteness();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-save-progress")){IndependentSaveProgress();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--review-native-identity")){NativeIdentityUsesRecordingLifetime();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--flac-restart-metadata")){RestartedFlacKeepsMetadataClaim();return 0;}
    if(argc==4&&!wcscmp(argv[1],L"--dump-ogg-listen-replay")) {ReplayCapturedOggListen(argv[2],argv[3]);return 0;}
    if(argc==6&&!wcscmp(argv[1],L"--dump-quality-replay")) {ReplayCapturedQuality(argv[2],argv[3],argv[4],argv[5]);return 0;}
    playback_id_payload_offset=0;
    if(argc==4&&!wcscmp(argv[1],L"--dump-replay")) {ReplayCapturedMp3(argv[2],argv[3]);return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--queue-epoch")) {ProductionCaptureQueueLossEpochs();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--stream-dump")) {DumpStreamHooks();return 0;}
    if(argc==2&&!wcscmp(argv[1],L"--pending-prior-control-stdin")) {
        const std::string payload(std::istreambuf_iterator<char>(std::cin),{});
        ClientPlaybackRecord record;std::string error;
        assert(ParseClientPlayback(payload,record,error)&&record.prior_control);
        StreamOwnedCompletion();return 0;
    }
    if(argc==2&&(!wcscmp(argv[1],L"--podcast-reuse")||!wcscmp(argv[1],L"--pause-continuity")||
        !wcscmp(argv[1],L"--podcast-end"))) {StreamOwnedCompletion();UnmatchedRetention();return 0;}
    assert(argc==1);
    FlacInputCompletesWithoutDecodedCallbacks();FlacRepeatedInputCompletesWithoutDecodedCallbacks();CompletedSourceQueueLossCannotRelabel();NativeIdentityUsesRecordingLifetime();PrefetchCannotSupplyCurrentIdentity();MatcherToleranceAndLogs();RetirementCannotManufactureMatch();RetiredMetadataCannotManufactureMatch();MatcherCandidateInventory();LateDurationContradictionStaysRejected();ReleaseAuditHonorsBudget();
    CaptureJournalRoundTrip();RestartedFlacKeepsMetadataClaim();Mp3BoundaryDoesNotProveCompleteness();IndependentSaveProgress();
    RecordingQueueCounters();StreamOwnedCompletion();UnmatchedRetention();UnmatchedWriterRejectsPartial();UnmatchedArchiveRetry();UnmatchedArchiveRetry(true);
    Connectivity();PodcastCatalog();PublicationLogging();ReadyUpgradesPreboundPlaybackGeneration();
    FinalCaptureRevalidation();CompactFailureIdentity();ProductionCaptureQueueLossEpochs();
    ContextSpecificCaptureQueueLoss();ReadyDurationCorroboratesDecoder();MissingPlaybackWarning();
    AdvertisementStageIsNotDownloadFailure();ObservePlayback();CoverageAndFailures();CaptureReasonLogs();
    SameContextFlacRestartBeginsFreshCapture();CaptureOwnershipLifecycle();OwnershipDiagnosticPrivacy();
    HookBoundaries();DecodedActivityQueue();TimingVerbosity();BasicModeProfiling();LoggingDisabled();DumpStreamHooks();
    puts("PASS: stream-owned recording lifecycle, unmatched retention/retry, codec validation and original hook semantics");
    return 0;
}
