#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/bounded_queue.h"
#include "../native/ogg_history_core.h"
#include <atomic>
static HANDLE probe_entered=nullptr,probe_release=nullptr;
static std::atomic<bool> hold_probe{false};
#include "../native/existing_quality.h"
#include "../native/flac_history_core.h"
#include "recording_fixture.h"
static std::atomic<unsigned> verification_reads{0},probe_attempts{0};
namespace history {
static std::wstring BlockingResolve(const std::wstring& path,const Catalog& catalog) {
    ++probe_attempts;
    if(hold_probe) {SetEvent(probe_entered);WaitForSingleObject(probe_release,INFINITE);}
    return ResolveRecordingPath(path,catalog);
}
static bool CountingRecording(const std::wstring& path,const Catalog& catalog,Quality& quality) {
    ++verification_reads;return ExistingRecording(path,catalog,quality);
}
}
#define ResolveRecordingPath BlockingResolve
#define ExistingRecording CountingRecording
#include "../native/song_destination.cpp"
#undef ResolveRecordingPath
#undef ExistingRecording
#include <cassert>
#include <cstdio>
static void AwaitProbe() {
    const auto deadline=GetTickCount64()+5000;
    for(;;) {
        {
            history::DestinationExclusive guard;
            if(history::result_serial==history::request_serial && !history::probing && !history::pending_request && !history::refresh_pending.load()) return;
        }
        assert(GetTickCount64()<deadline);Sleep(1);
    }
}
static void SubmitSong(std::uint64_t identity,const std::wstring& root,const history::Catalog& item,bool music_folder,unsigned generation) {
    history::PublishSongDestination(identity,root,item,music_folder,generation);
    history::RefreshSongDestination();
    const auto deadline=GetTickCount64()+5000;
    for(;;) {
        {history::DestinationExclusive guard;
         if(history::result_serial==history::request_serial && !history::probing && !history::pending_request && !history::refresh_pending.load())return;}
        assert(GetTickCount64()<deadline);Sleep(10);
        history::PublishSongDestination(identity,root,item,music_folder,generation);
    }
}
static std::string Utf8Text(const std::wstring& text) {
    const auto size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),size,nullptr,nullptr);return result;
}
static std::vector<uint8_t> OggFixture() {
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path=exe;path.resize(path.find_last_of(L'\\')+1);path+=L"low.ogg";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);std::vector<uint8_t> bytes(GetFileSize(file,nullptr));DWORD read=0;
    assert(ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr) && read==bytes.size());CloseHandle(file);return bytes;
}
static void WriteAudio(const std::wstring& file,const history::Catalog& item) {
    using namespace history;
    assert(EnsureDirectory(file.substr(0,file.find_last_of(L'\\'))));
    Tags tags;tags.fields={{"TITLE",Utf8Text(item.title)},{"ARTIST",Utf8Text(item.artist)},{"ALBUM",Utf8Text(item.album)},
        {"TRACKNUMBER",std::to_string(item.track)},{"DISCNUMBER",std::to_string(item.disc)},
        {"SPOTIFY_URI",item.spotify_uri},{"HISTORY_MEDIA_DURATION",std::to_string(item.duration)},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE",file.find(L".ogg")!=std::wstring::npos ? "native compressed Ogg pages" : "native compressed FLAC input"}};
    std::vector<uint8_t> bytes;std::string error;
    assert(file.find(L".ogg")!=std::wstring::npos ? TagOgg(OggFixture(),tags,bytes,error) : TagFlac(recording_fixture::fixture(),tags,bytes,error));
    HANDLE handle=CreateFileW(WindowsPath(file).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    assert(handle!=INVALID_HANDLE_VALUE);
    DWORD written=0;assert(WriteFile(handle,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size());CloseHandle(handle);
}
int main() {
    using namespace history;
    Catalog item;item.artist=L"Album Artist";item.album=L"Album: Deluxe";item.title=L"Song";item.track=3;item.disc=1;item.duration=1;item.spotify_uri="spotify:track:song-probe";
    wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);
    const auto root=std::wstring(temp)+L"Floggfy-existing-song-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES);
    probe_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);probe_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    StartSongDestinationWorker(false);
    PublishSongDestination(7,root,item,false,11);
    assert(WaitForSingleObject(probe_entered,50)==WAIT_TIMEOUT); // Menu=0 does no probes.
    StartSongDestinationWorker(true);
    hold_probe=true;
    struct Sample {unsigned index=0;};
    BoundedQueue<Sample,32> samples;
    struct Submit {std::wstring root;Catalog item;HANDLE returned;BoundedQueue<Sample,32>* samples;};
    Submit submit{root,item,CreateEventW(nullptr,TRUE,FALSE,nullptr),&samples};
    HANDLE submit_thread=CreateThread(nullptr,0,[](void* value)->DWORD {
        auto& request=*static_cast<Submit*>(value);
        PublishSongDestination(7,request.root,request.item,false,11);SetEvent(request.returned);
        for(unsigned i=0;i<200;++i) {
            Sample sample{i};while(!request.samples->Push(std::move(sample)))Sleep(1);
            Sleep(20);
        }
        return 0;
    },&submit,0,nullptr);
    assert(WaitForSingleObject(probe_entered,1000)==WAIT_OBJECT_0);
    const bool returned_without_storage=WaitForSingleObject(submit.returned,100)==WAIT_OBJECT_0;
    Listen observed;unsigned drained=0;const auto blocked_start=GetTickCount64();
    while(GetTickCount64()-blocked_start<3300) {
        Sample sample;
        if(samples.Pop(sample)) {
            observed.Observe("capture",sample.index*.02,100,true,sample.index*.02);++drained;
        } else Sleep(1);
        PublishSongDestination(9,root,item,false,12); // Bounded latest-request replacement while storage stalls.
    }
    assert(drained>100 && observed.eligible && WaitForSingleObject(probe_release,0)==WAIT_TIMEOUT);
    {
        DestinationExclusive guard;
        assert(pending_request && pending_request->identity==9 && latest_request->identity==9);
    }
    hold_probe=false;SetEvent(probe_release);
    while(WaitForSingleObject(submit_thread,0)!=WAIT_OBJECT_0) {Sample sample;samples.Pop(sample);Sleep(1);}
    AwaitProbe();
    std::wstring stale;
    assert(!ReadSongDestination(7,11,stale));
    {DestinationExclusive guard;assert(destination_identity==9 && destination_generation==12);}

    CloseHandle(submit_thread);CloseHandle(submit.returned);
    assert(returned_without_storage); // Capture submission must not perform a blocked probe.
    std::wstring saved_file=L"retained";
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==L"retained");
    SubmitSong(7,root,item,false,11);
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==L"retained");
    assert(GetFileAttributesW(root.c_str())==INVALID_FILE_ATTRIBUTES); // Probes never create missing folders.
    for(bool music_folder:{false,true}) {
        for(auto kind:{MediaKind::Music,MediaKind::VariousArtists}) {
            item.kind=kind;
            const auto flac=OutputPath(root,item,L".flac",music_folder);
            const auto ogg=OutputPath(root,item,L".ogg",music_folder);
            const auto folder=flac.substr(0,flac.find_last_of(L'\\'));
            assert(EnsureDirectory(folder));
            SubmitSong(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file)); // An empty existing folder is not a saved song.
            assert(CreateDirectoryW(WindowsPath(flac).c_str(),nullptr));
            SubmitSong(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file)); // A directory named *.flac is not audio.
            assert(RemoveDirectoryW(WindowsPath(flac).c_str()));
            WriteAudio(ogg,item);SubmitSong(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
            WriteAudio(flac,item);SubmitSong(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==flac); // Prefer FLAC when both exist.
            assert(DeleteFileW(WindowsPath(flac).c_str()));
            SubmitSong(7,root,item,music_folder,11);
            assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
            assert(DeleteFileW(WindowsPath(ogg).c_str()));
            SubmitSong(7,root,item,music_folder,11);
            assert(!ReadSongDestination(7,11,saved_file));
        }
    }
    const auto ogg=OutputPath(root,item,L".ogg",true);
    WriteAudio(ogg,item);SubmitSong(7,root,item,true,11);
    assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
    ResetEvent(probe_entered);ResetEvent(probe_release);hold_probe=true;RefreshSongDestination();
    assert(WaitForSingleObject(probe_entered,1000)==WAIT_OBJECT_0);
    const auto unchanged_serial=request_serial.load();
    for(unsigned i=0;i<1000;++i)PublishSongDestination(7,root,item,true,11);
    assert(request_serial.load()==unchanged_serial); // Unchanged requests never starve slow verification.
    hold_probe=false;SetEvent(probe_release);AwaitProbe();
    assert(ReadSongDestination(7,11,saved_file) && saved_file==ogg);
    const auto reads_before_cached=verification_reads.load();
    Sleep(450);PublishSongDestination(7,root,item,true,11);AwaitProbe();
    assert(ReadSongDestination(7,11,saved_file) && verification_reads.load()==reads_before_cached);
    RefreshSongDestination();AwaitProbe();assert(verification_reads.load()>reads_before_cached);
    const auto reads_before_changed=verification_reads.load();
    assert(DeleteFileW(WindowsPath(ogg).c_str()));WriteAudio(ogg,item);
    Sleep(450);PublishSongDestination(7,root,item,true,11);AwaitProbe();
    assert(ReadSongDestination(7,11,saved_file) && verification_reads.load()>reads_before_changed);
    const auto before=saved_file;
    AcquireSRWLockExclusive(&destination_lock);
    const auto start=GetTickCount64();
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==before);
    assert(GetTickCount64()-start<100);
    ReleaseSRWLockExclusive(&destination_lock);
    AcquireSRWLockExclusive(&destination_lock);
    const auto submit_start=GetTickCount64();PublishSongDestination(8,root,item,true,11);
    assert(GetTickCount64()-submit_start<100);ReleaseSRWLockExclusive(&destination_lock);
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==before);
    SubmitSong(7,root,item,true,11);
    auto changed_identity=item;changed_identity.spotify_uri="spotify:track:new-recording-same-label";
    AcquireSRWLockExclusive(&destination_lock);
    PublishSongDestination(7,root,changed_identity,true,11);ReleaseSRWLockExclusive(&destination_lock);
    assert(!ReadSongDestination(7,11,saved_file)); // Same display key, different recording intent.
    SubmitSong(7,root,item,true,11);
    assert(!ReadSongDestination(8,11,saved_file) && saved_file==before);
    assert(!ReadSongDestination(7,12,saved_file) && saved_file==before);
    assert(DeleteFileW(WindowsPath(ogg).c_str()));
    assert(ReadSongDestination(7,11,saved_file) && saved_file==before); // RAM read does no filesystem probes.
    destination_updated=GetTickCount64()-4000;
    assert(!ReadSongDestination(7,11,saved_file) && saved_file==before);
    auto other=item;other.spotify_uri="spotify:track:other-probe";
    WriteAudio(ogg,other); // Preserve a different recording occupying the legacy filename.
    const auto alternate=ResolveRecordingPath(ogg,item);assert(alternate!=ogg && !alternate.empty());
    WriteAudio(alternate,item);SubmitSong(7,root,item,true,11);
    assert(ReadSongDestination(7,11,saved_file) && saved_file==alternate);
    assert(DeleteFileW(WindowsPath(alternate).c_str()));assert(DeleteFileW(WindowsPath(ogg).c_str()));
    ResetEvent(probe_entered);ResetEvent(probe_release);hold_probe=true;RefreshSongDestination();
    assert(WaitForSingleObject(probe_entered,1000)==WAIT_OBJECT_0);
    const auto disable_start=GetTickCount64();StartSongDestinationWorker(false);
    assert(GetTickCount64()-disable_start<100);
    const auto attempts_when_disabled=probe_attempts.load();
    PublishSongDestination(99,root,item,true,55);
    hold_probe=false;SetEvent(probe_release);
    const auto disabled_deadline=GetTickCount64()+5000;
    for(;;) {bool done=false;{DestinationExclusive guard;done=!probing;}if(done)break;assert(GetTickCount64()<disabled_deadline);Sleep(1);}
    assert(probe_attempts.load()==attempts_when_disabled && !ReadSongDestination(7,11,saved_file));
    StartSongDestinationWorker(true);
    PublishSongDestination(0,L"",{},false,11);
    assert(!ReadSongDestination(7,11,saved_file) && !ReadSongDestination(0,11,saved_file));
    StartSongDestinationWorker(false);
    CloseHandle(probe_entered);CloseHandle(probe_release);
    std::puts("PASS: blocked storage for >3 seconds leaves bounded capture production/drain/listen advancing; latest request wins and Menu=0 disables probes");
    std::puts("PASS: exact existing saver files only, missing/empty/directory targets disabled, FLAC preference, Ogg fallback, identity/settings/freshness binding and nonblocking RAM reads");
}
