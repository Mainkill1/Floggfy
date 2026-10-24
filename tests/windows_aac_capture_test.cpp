// Combined AAC gate: core parser/container tests plus actual hook/Windows
// publication integration. Runtime never invokes Spotify internals.
#include "../native/audio_history.cpp"
#include "../native/history_settings.cpp"
#include "spac_fixture.h"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <cassert>
#include <fstream>
int RunSpacStreamTests();
int RunAacContainerTests();
int RunMediaSnapshotTests();
namespace {
bool extra_metadata=false;
unsigned native_push_calls=0;
bool native_reject=false;
void* NativeInitialize(void* context,bool){return context;}
void* NativeDestroy(void* context,unsigned){return context;}
void* NativePush(void*,void* status,const AacView*) {
    ++native_push_calls;const uint64_t value[]={native_reject?7u:0u,0x12345678};memcpy(status,value,sizeof(value));return status;
}
std::vector<uint8_t> Read(const std::wstring& path) {
    history::settings_file::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    assert(file.value!=INVALID_HANDLE_VALUE);const auto size=GetFileSize(file.value,nullptr);assert(size!=INVALID_FILE_SIZE);
    std::vector<uint8_t> bytes(size);DWORD count=0;assert(ReadFile(file.value,bytes.data(),size,&count,nullptr)&&count==size);return bytes;
}
void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    history::settings_file::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr));
    assert(file.value!=INVALID_HANDLE_VALUE);DWORD count=0;
    assert(WriteFile(file.value,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size());
}
std::string Field(const Tags& tags,const char* key) {
    for(const auto& field:tags.fields)if(field.first==key)return field.second;
    return {};
}
std::unique_ptr<Capture> Captured(const std::vector<uint8_t>& source) {
    CaptureBuffers buffers;PlaybackQualityTracker quality;
    void* context=reinterpret_cast<void*>(0x1234);
    assert(AacInitializeHook(context,false)==context);
    // Deliberately fragment a page across borrowed buffers.
    size_t at=0;
    while(at<source.size()) {
        const auto amount=std::min(size_t(17),source.size()-at);AacView view{source.data()+at,amount};
        uint64_t status[2]={9,9};const auto calls=native_push_calls;
        assert(AacPushHook(context,status,&view)==status&&native_push_calls==calls+1);
        assert(status[0]==0&&status[1]==0x12345678);at+=amount;
        Slot observed;
        while(Pop(observed))ProcessCaptureEvent(observed,buffers,quality,false,true,CaptureEpoch());
    }
    assert(AacDestroyHook(context,0)==context);
    Slot observed;while(Pop(observed))ProcessCaptureEvent(observed,buffers,quality,false,true,CaptureEpoch());
    assert(buffers.active.empty()&&buffers.ready.size()==1&&buffers.ready[0]->spac.Complete());
    return std::move(buffers.ready[0]);
}
void TestPipeline(const std::wstring& root) {
    const auto source=spac_fixture::stream(29,true);
    auto capture=Captured(source);
    Heard heard;heard.media.title=L"Native AAC";heard.media.artist=L"Artist";heard.media.album=L"Album";
    heard.media.duration=capture->Duration();heard.start=capture->born;heard.finish=heard.start+heard.media.duration;heard.generation=1;
    const auto catalog=CatalogFor(heard.media);const auto path=OutputPath(root,catalog,L".m4a",false);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
    RecordingFile inspected;assert(ReadRecording(path,inspected)&&inspected.complete&&inspected.owned&&MatchesRecording(inspected,catalog));
    assert(inspected.quality.codec==Codec::Aac&&!inspected.has_artwork&&Field(inspected.tags,"TITLE")=="Native AAC");
    auto before=Read(path);const auto old=capture->Encoding();
    assert(!HigherQuality(old,old));
    capture=Captured(source);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped&&Read(path)==before);
    std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.size()==1&&recent[0].path==path);
    extra_metadata=true;assert(SetUpdateExistingMetadata(false));
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped&&Read(path)==before);
    assert(SetUpdateExistingMetadata(true));
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
    assert(ReadRecording(path,inspected)&&inspected.complete&&Field(inspected.tags,"LYRICS")=="[00:01.000]Synthetic words"&&inspected.has_artwork);
    assert(Field(inspected.tags,"GENRE")=="Rock");before=Read(path);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped&&Read(path)==before);
    assert(ReadRecentDownloads(recent)&&recent.size()==1); // Enrichment adds no download row.
    extra_metadata=false;
    // Prefer the same recording already saved as lossless; never create a low
    // quality AAC counterpart or overwrite the existing FLAC.
    assert(DeleteFileW(path.c_str())&&SetUpdateExistingMetadata(false));
    const auto flac=OutputPath(root,catalog,L".flac",false);
    Tags tags;tags.fields={{"TITLE","Native AAC"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",catalog.spotify_uri},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
    std::vector<uint8_t> high;std::string error;assert(TagFlac(recording_fixture::fixture(),tags,high,error));Write(flac,high);
    const auto resolved=ResolveRecordingDestination(flac,catalog);
    assert(resolved.state==RecordingDestinationState::Existing && resolved.path==flac &&
        resolved.recording.complete && resolved.recording.quality.codec==Codec::Flac);
    // A sharing violation is unknown, never evidence that the preferred file
    // is absent. Do not create an AAC counterpart while this file is locked.
    HANDLE locked=CreateFileW(flac.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(ResolveRecordingDestination(flac,catalog).state==RecordingDestinationState::Blocked);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed &&
        GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    CloseHandle(locked);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped&&Read(flac)==high&&GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    // Interrupted-upgrade backups retain the same uncertainty guarantee. Check
    // both ordinary and deterministic suffixed backup names.
    const auto backup=flac+L".previous";
    assert(MoveFileW(flac.c_str(),backup.c_str()));
    locked=CreateFileW(backup.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed &&
        GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    CloseHandle(locked);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped && Read(flac)==high &&
        GetFileAttributesW(backup.c_str())==INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(flac.c_str()));
    auto unrelated_tags=tags;
    for(auto& field:unrelated_tags.fields)if(field.first=="SPOTIFY_URI")field.second="spotify:track:other";
    std::vector<uint8_t> other_flac;assert(TagFlac(recording_fixture::fixture(),unrelated_tags,other_flac,error));
    Write(flac,other_flac);
    const auto flac_suffix=ResolveRecordingDestination(flac,catalog);
    assert(flac_suffix.state==RecordingDestinationState::Vacant && flac_suffix.path!=flac);
    Write(flac_suffix.path,high);assert(DeleteFileW(flac.c_str()));
    locked=CreateFileW(flac_suffix.path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed &&
        GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    CloseHandle(locked);
    const auto suffix_backup=flac_suffix.path+L".previous";
    assert(MoveFileW(flac_suffix.path.c_str(),suffix_backup.c_str()));
    locked=CreateFileW(suffix_backup.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed &&
        GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    CloseHandle(locked);
    const auto reused=ResolveRecordingDestination(flac,catalog);
    assert(reused.state==RecordingDestinationState::Existing && reused.path==flac_suffix.path && reused.recording.complete);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped && Read(flac_suffix.path)==high &&
        GetFileAttributesW(flac.c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(flac_suffix.path.c_str()));
    // Apply the same fail-closed rule to a locked Vorbis candidate even when
    // there is no FLAC, and reuse the verified collision suffix after deletion.
    const auto ogg=OutputPath(root,catalog,L".ogg",false);
    tags.fields.back()={"HISTORY_CAPTURE","native compressed Ogg pages"};
    std::vector<uint8_t> vorbis;
    assert(TagOgg(recording_fixture::ogg(),tags,vorbis,error));Write(ogg,vorbis);
    locked=CreateFileW(ogg.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed &&
        GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES);
    CloseHandle(locked);
    assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped && Read(ogg)==vorbis);
    assert(DeleteFileW(ogg.c_str())&&SetUpdateExistingMetadata(true));
    // Recording identity still disambiguates AAC filename collisions.
    AacInfo info;std::vector<std::vector<uint8_t>> packets;
    assert(ReadSpac(source,info,packets,error));
    tags.fields.back()={"HISTORY_CAPTURE","native compressed SpAC AAC packets"};
    for(auto& field:tags.fields)if(field.first=="SPOTIFY_URI")field.second="spotify:track:unrelated";
    std::vector<uint8_t> unrelated;assert(MakeAacM4a(info,packets,tags,unrelated,error));Write(path,unrelated);
    const auto alternate=ResolveRecordingPath(path,catalog);assert(!alternate.empty()&&alternate!=path);
    capture=Captured(source);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved&&Read(path)==unrelated);
    assert(ReadRecording(alternate,inspected)&&MatchesRecording(inspected,catalog));
    assert(DeleteFileW(path.c_str())&&DeleteFileW(alternate.c_str()));
}
void RealSample(const std::wstring& input,const std::wstring& output) {
    const auto source=Read(input);AacInfo info;std::vector<std::vector<uint8_t>> packets;std::string error;
    assert(ReadSpac(source,info,packets,error));Tags tags;
    tags.fields={{"TITLE","Native AAC sample"},{"HISTORY_CAPTURE","native compressed SpAC AAC packets"},{"HISTORY_COMPLETE_LISTEN","1"}};
    std::vector<uint8_t> m4a;assert(MakeAacM4a(info,packets,tags,m4a,error));
    AacFile inspected;assert(InspectAacM4a(m4a.data(),m4a.size(),inspected)&&inspected.complete);
    assert(inspected.info.config==info.config&&inspected.info.total_samples==info.total_samples&&inspected.info.priming==info.priming);
    uint64_t audio_bytes=0;for(const auto& p:packets)audio_bytes+=p.size();assert(audio_bytes==inspected.audio_bytes);
    bool matched_media=false;
    for(size_t at=0;at<m4a.size();) {
        assert(m4a.size()-at>=8);
        uint32_t size=0;for(unsigned i=0;i<4;++i)size=(size<<8)|m4a[at+i];
        assert(size>=8 && size<=m4a.size()-at);
        if(!memcmp(m4a.data()+at+4,"mdat",4)) {
            size_t cursor=at+8;
            for(const auto& packet:packets) {
                assert(packet.size()<=at+size-cursor && !memcmp(m4a.data()+cursor,packet.data(),packet.size()));
                cursor+=packet.size();
            }
            assert(cursor==at+size && !matched_media);matched_media=true;
        }
        at+=size;
    }
    assert(matched_media); // Real input AUs survive this exact writer revision.
    Write(output,m4a);
    printf("REAL SAMPLE: rate=%u channels=%u samples=%u priming=%u units=%zu audio_bytes=%llu m4a_bytes=%zu\n",
        info.rate,info.channels,info.total_samples,info.priming,packets.size(),static_cast<unsigned long long>(audio_bytes),m4a.size());
}
}
namespace history {
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media&,Catalog& catalog){catalog.spotify_uri="spotify:track:aac-fixture";}
void EnrichTags(const Media&,Tags& tags) {
    if(!extra_metadata||!GetSettings().metadata)return;
    tags.fields.emplace_back("GENRE","Rock");tags.fields.emplace_back("LYRICS","[00:01.000]Synthetic words");
    tags.fields.emplace_back("FLOGGFY_ARTWORK_SOURCE","spotify-client");tags.cover=artwork_fixture::png();tags.mime="image/png";
}
}
int wmain(int argc,wchar_t** argv) {
    assert(RunSpacStreamTests()==0&&RunAacContainerTests()==0&&RunMediaSnapshotTests()==0);
    original_aac_initialize=NativeInitialize;original_aac_push=NativePush;original_aac_destroy=NativeDestroy;
    assert(!Settings{}.aac&&!AACEnabled());uint64_t status[2]={9,9};
    assert(AacPushHook(nullptr,status,nullptr)==status&&native_push_calls==1&&count==0); // Disabled: no input read or event.
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe(executable);const auto directory=exe.substr(0,exe.find_last_of(L'\\'));
    const auto ini=directory+L"\\SpotifyHistory.ini",root=directory+L"\\Music";
    assert(CreateDefaultIni(ini));assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,ini.c_str())==0);
    assert(WritePrivateProfileStringW(L"To Disk",L"AAC",L"1",ini.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Menu",L"0",ini.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"0",ini.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Save Location",root.c_str(),ini.c_str()));
    InitSettings(nullptr);assert(GetSettings().aac&&AACEnabled());
    assert(SetDownloads(false)&&!AACEnabled());assert(SetDownloads(true)&&AACEnabled());
    assert(FlushSettings(5000)&&GetPrivateProfileIntW(L"To Disk",L"AAC",9,ini.c_str())==1);
    queue=static_cast<Slot*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Slot)*capacity));assert(queue);
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    TestPipeline(root);
    // Invalid native status, partial/oversized feeds, and epoch changes cannot
    // generate a ready recording. Integrity checks remain in the core cases.
    CaptureBuffers buffers;PlaybackQualityTracker quality;Slot begin{};
    begin.context=99;begin.kind=AudioEventKind::AacBegin;begin.capture=true;begin.epoch=CaptureEpoch();begin.time=Now();
    ProcessCaptureEvent(begin,buffers,quality,false,true,CaptureEpoch()+1);assert(buffers.active.empty());
    ProcessCaptureEvent(begin,buffers,quality,false,true,CaptureEpoch());assert(buffers.active.size()==1);
    native_reject=true;
    AacView rejected{reinterpret_cast<const uint8_t*>("SpAC"),4};uint64_t failed_status[2]={0,0};
    assert(AacPushHook(reinterpret_cast<void*>(99),failed_status,&rejected)==failed_status&&failed_status[0]==7);
    Slot observed;while(Pop(observed))ProcessCaptureEvent(observed,buffers,quality,false,true,CaptureEpoch());
    assert(buffers.active.empty()&&buffers.ready.empty());native_reject=false;
    begin.kind=AudioEventKind::AacBegin;ProcessCaptureEvent(begin,buffers,quality,false,true,CaptureEpoch());
    begin.kind=AudioEventKind::AacDestroy;ProcessCaptureEvent(begin,buffers,quality,false,true,CaptureEpoch());assert(buffers.active.empty()&&buffers.ready.empty());
    if(argc>=3)RealSample(argv[1],argv[2]);else assert(argc==1);
    if(argc==4) {
        // Map the supplied client for static capability discovery only. No
        // Spotify initialization, imports or decoder functions are invoked.
        const auto client=LoadLibraryExW(argv[3],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(client);
        const auto base=reinterpret_cast<const uint8_t*>(client);
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        spotify::AacHookTargets targets;
        assert(spotify::DiscoverAacHookTargets(base,nt->OptionalHeader.SizeOfImage,targets)==spotify::DiscoveryResult::Found);
        assert(targets.initialize&&targets.push&&targets.destroy);
        spotify::HookTargets existing;
        assert(spotify::DiscoverHookTargets(base,nt->OptionalHeader.SizeOfImage,existing)==spotify::DiscoveryResult::Found);
        FreeLibrary(client);
    } else assert(argc==1||argc==3);
    spotify::AacHookTargets invalid{1,2,3};const uint8_t bad_image[4]={};
    assert(spotify::DiscoverAacHookTargets(bad_image,sizeof(bad_image),invalid)==spotify::DiscoveryResult::InvalidImage);
    assert(!invalid.initialize&&!invalid.push&&!invalid.destroy);
    CloseHandle(event);event=nullptr;HeapFree(GetProcessHeap(),0,queue);queue=nullptr;
    assert(FlushSettings(5000));
    puts("PASS: optional AAC defaults off; hooks preserve native ABI; complete AAC publishes atomically as M4A; duplicate/preferred-file retention, identity collisions and metadata-only opt-out work");
    return 0;
}
