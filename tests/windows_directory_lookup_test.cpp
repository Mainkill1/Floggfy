// One focused executable: real Windows directory enumeration and file fixtures.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cwchar>
static unsigned listings=0,attribute_checks=0,recording_opens=0,signature_checks=0;
static bool deny_listing=false,interrupt_listing=false,endless_listing=false;
static HANDLE WINAPI CountFindFirst(LPCWSTR path,FINDEX_INFO_LEVELS level,LPVOID data,
    FINDEX_SEARCH_OPS search,LPVOID filter,DWORD flags) {
    ++listings;
    if(deny_listing){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    return FindFirstFileExW(path,level,data,search,filter,flags);
}
static BOOL WINAPI CountFindNext(HANDLE handle,LPWIN32_FIND_DATAW data) {
    if(interrupt_listing){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(endless_listing){std::wcscpy(data->cFileName,L"entry");return TRUE;}
    return FindNextFileW(handle,data);
}
static DWORD WINAPI CountAttributes(LPCWSTR path) {
    ++attribute_checks;return GetFileAttributesW(path);
}
static HANDLE WINAPI CountOpen(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,
    DWORD creation,DWORD attributes,HANDLE template_file) {
    if(creation==OPEN_EXISTING && (access&GENERIC_READ))++recording_opens;
    return CreateFileW(path,access,sharing,security,creation,attributes,template_file);
}
#define FindFirstFileExW CountFindFirst
#define FindNextFileW CountFindNext
#define GetFileAttributesW CountAttributes
#define CreateFileW CountOpen
#include "../native/existing_quality.cpp"
#undef CreateFileW
#undef GetFileAttributesW
#undef FindNextFileW
#undef FindFirstFileExW
#include "../native/audio_history.cpp"
#include "../native/history_settings.cpp"
static BOOL WINAPI CountSignature(LPCWSTR path,GET_FILEEX_INFO_LEVELS level,LPVOID data) {
    ++signature_checks;return GetFileAttributesExW(path,level,data);
}
#define GetFileAttributesExW CountSignature
#include "../native/song_destination.cpp"
#undef GetFileAttributesExW
#include "recording_fixture.h"
using namespace history;
namespace {
void ResetCounts(){listings=attribute_checks=recording_opens=signature_checks=0;}
void Write(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
    HANDLE file=CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);DWORD written=0;
    assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());CloseHandle(file);
}
Catalog Song(const wchar_t* title) {
    Catalog c;c.artist=L"Artist";c.album=L"Album";c.title=title;c.track=3;c.disc=2;c.duration=1;
    c.spotify_uri="spotify:track:"+Utf8(title);return c;
}
std::vector<uint8_t> Audio(const Catalog& c,unsigned bits=16,bool ogg=false) {
    Tags tags;tags.fields={{"TITLE",Utf8(c.title)},{"ARTIST","Artist"},{"ALBUMARTIST","Artist"},
        {"ALBUM","Album"},{"SPOTIFY_URI",c.spotify_uri},{"HISTORY_MEDIA_DURATION","1"},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE",ogg?"native compressed Ogg pages":"native compressed FLAC input"}};
    std::vector<uint8_t> bytes;std::string error;
    assert(ogg?TagOgg(recording_fixture::ogg(),tags,bytes,error):TagFlac(recording_fixture::fixture(44100,bits),tags,bytes,error));
    return bytes;
}
void Lookups(const std::wstring& root) {
    const auto c=Song(L"Lookup"),other=Song(L"Other");
    const auto base=OutputPath(root,c,L".flac"),suffix=base.substr(0,base.size()-5)+RecordingSuffix(c)+L".flac";
    Write(base,Audio(other));Write(suffix,Audio(c,24));
    ResetCounts();auto resolved=ResolveRecordingDestination(base,c);
    assert(resolved.state==RecordingDestinationState::Existing&&resolved.path==suffix&&resolved.recording.quality.bits==24);
    assert(listings==1&&attribute_checks==0&&recording_opens==2); // No probes for absent aliases.
    assert(DeleteFileW(WindowsPath(base).c_str()));
    ResetCounts();resolved=ResolveRecordingDestination(base,c);
    assert(resolved.path==suffix&&resolved.recording.quality.bits==24&&listings==1&&recording_opens==1&&attribute_checks==0);
    HANDLE held=CreateFileW(WindowsPath(suffix).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
    assert(ResolveRecordingDestination(base,c).state==RecordingDestinationState::Blocked);CloseHandle(held);
    assert(DeleteFileW(WindowsPath(suffix).c_str()));
    // Windows case-insensitive filenames remain candidates from the listing.
    auto uppercase=base;uppercase.replace(uppercase.size()-5,5,L".FLAC");Write(uppercase,Audio(c));
    ResetCounts();assert(ResolveRecordingDestination(base,c).path==base&&listings==1&&recording_opens==1&&attribute_checks==0);
    assert(DeleteFileW(WindowsPath(uppercase).c_str()));
    Write(base+L".previous",Audio(c,24));ResetCounts();resolved=ResolveRecordingDestination(base,c);
    assert(resolved.path==base&&resolved.recording.quality.bits==24&&listings==1);
    assert(DeleteFileW(WindowsPath(base).c_str()));
    // A directory occupying a candidate is not a safe vacancy.
    assert(CreateDirectoryW(WindowsPath(base).c_str(),nullptr));
    assert(ResolveRecordingDestination(base,c).state==RecordingDestinationState::Blocked);
    assert(RemoveDirectoryW(WindowsPath(base).c_str()));
    const auto missing=root+L"\\Absent\\Nested\\No file.flac";
    ResetCounts();assert(ResolveRecordingDestination(missing,c).state==RecordingDestinationState::Vacant);
    assert(listings==1&&recording_opens==0&&attribute_checks==0);
    deny_listing=true;assert(ResolveRecordingDestination(base,c).state==RecordingDestinationState::Blocked);deny_listing=false;
    Write(base,Audio(c));interrupt_listing=true;
    assert(ResolveRecordingDestination(base,c).state==RecordingDestinationState::Blocked);interrupt_listing=false;
    endless_listing=true;assert(ResolveRecordingDestination(base,c).state==RecordingDestinationState::Blocked);endless_listing=false;
    assert(DeleteFileW(WindowsPath(base).c_str()));
}
void Conversion(const std::wstring& root) {
    const auto c=Song(L"Convert");const auto title=OutputPath(root,c,L".ogg");
    const auto slash=title.find_last_of(L'\\');const auto old=title.substr(0,slash+1)+L"03 - "+title.substr(slash+1);
    Write(old,Audio(c,16,true));
    {ExclusiveSrw guard(history::lock);history::settings.root=root;history::settings.convert_numbered_filenames=true;
     history::settings.track_number_in_filename=false;history::settings.generation=7;}
    FilenameCheck check{c,GetSettings(),filename_check_serial.load()};
    ResetCounts();ProcessFilenameCheck(check);
    assert(listings==1&&attribute_checks==0); // One list shared by FLAC/Ogg/M4A checks.
    assert(GetFileAttributesW(WindowsPath(old).c_str())==INVALID_FILE_ATTRIBUTES);
    RecordingFile saved;assert(ReadRecording(title,saved)&&saved.complete&&MatchesRecording(saved,c));
    Write(old,Audio(c,16,true)); // Occupied canonical target is never replaced/deleted.
    ResetCounts();ProcessFilenameCheck(check);assert(listings==1&&attribute_checks==0);
    assert(GetFileAttributesW(WindowsPath(old).c_str())!=INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(WindowsPath(old).c_str())&&DeleteFileW(WindowsPath(title).c_str()));
    // All formats absent still requires only one directory enumeration.
    ResetCounts();ProcessFilenameCheck(check);assert(listings==1&&attribute_checks==0&&recording_opens==0);

    // The third supported family uses the same listing after FLAC/Ogg misses.
    const auto m4a=OutputPath(root,c,L".m4a"),m4a_old=m4a.substr(0,slash+1)+L"03 - "+m4a.substr(slash+1);
    Tags tags;tags.fields={{"SPOTIFY_URI",c.spotify_uri},{"HISTORY_MEDIA_DURATION","1"},
        {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed SpAC AAC packets"}};
    AacInfo info{44100,2,2048,100,2500,{0xeb,0x8a,0x08,0x00}};
    std::vector<uint8_t> bytes;std::string error;
    assert(MakeAacM4a(info,{{0x21,0x10,0x04},{0x21,0x10,0x04}},tags,bytes,error));Write(m4a_old,bytes);
    ResetCounts();ProcessFilenameCheck(check);assert(listings==1&&attribute_checks==0);
    assert(GetFileAttributesW(WindowsPath(m4a_old).c_str())==INVALID_FILE_ATTRIBUTES);
    assert(ReadRecording(m4a,saved)&&saved.complete&&MatchesRecording(saved,c));
    assert(DeleteFileW(WindowsPath(m4a).c_str()));
}
void MenuLookup(const std::wstring& root) {
    const auto c=Song(L"Menu");DestinationRequest request;request.catalog=c;request.serial=41;request.generation=7;
    request.flac=OutputPath(root,c,L".flac");request.ogg=OutputPath(root,c,L".ogg");request.aac=OutputPath(root,c,L".m4a");
    const auto slash=request.ogg.find_last_of(L'\\');const auto old=request.ogg.substr(0,slash+1)+L"03 - "+request.ogg.substr(slash+1);
    Write(old,Audio(c,16,true));destination_enabled=true;request_serial=41;
    VerificationCache cache;ResetCounts();assert(ProbeSavedSong(request,cache)==old);
    assert(listings==1&&recording_opens==1&&attribute_checks==0);
    ResetCounts();
    for(unsigned i=0;i<3;++i)assert(ProbeSavedSong(request,cache)==old);
    assert(listings==0&&recording_opens==0&&signature_checks==3);
    // A changed signature requires discovery and complete verification again.
    HANDLE edit=CreateFileW(WindowsPath(old).c_str(),FILE_WRITE_ATTRIBUTES,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(edit!=INVALID_HANDLE_VALUE);
    const auto previous=cache.entries.front().signature.data.ftLastWriteTime;
    ULARGE_INTEGER changed{};changed.LowPart=previous.dwLowDateTime;changed.HighPart=previous.dwHighDateTime;changed.QuadPart+=10000000;
    FILETIME changed_time{changed.LowPart,changed.HighPart};assert(SetFileTime(edit,nullptr,nullptr,&changed_time));CloseHandle(edit);
    ResetCounts();assert(ProbeSavedSong(request,cache)==old&&listings==1&&recording_opens==1);
    assert(DeleteFileW(WindowsPath(old).c_str()));ResetCounts();
    assert(ProbeSavedSong(request,cache).empty()&&listings==1&&recording_opens==0);
    ResetCounts();
    for(unsigned i=0;i<3;++i)assert(ProbeSavedSong(request,cache).empty());
    assert(listings==0&&recording_opens==0&&signature_checks==0);
    // External creation is discovered after the short miss cooldown expires.
    Write(old,Audio(c,16,true));ResetCounts();assert(ProbeSavedSong(request,cache).empty()&&listings==0);
    cache.unavailable_updated=GetTickCount64()-5000;
    ResetCounts();assert(ProbeSavedSong(request,cache)==old&&listings==1&&recording_opens==1);
    assert(DeleteFileW(WindowsPath(old).c_str()));assert(ProbeSavedSong(request,cache).empty());
    Write(old,Audio(c,16,true));
    // Publication refresh invalidates both positive and unavailable results.
    destination_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(destination_event);
    RefreshSongDestination();request.serial=request_serial.load();request.revision=cache_revision.load();
    ResetCounts();assert(ProbeSavedSong(request,cache)==old&&listings==1&&recording_opens==1);
    RefreshSongDestination();request.serial=request_serial.load();request.revision=cache_revision.load();
    ResetCounts();assert(ProbeSavedSong(request,cache)==old&&listings==1&&recording_opens==1);
    auto next=request;next.catalog=Song(L"New menu song");
    next.flac=OutputPath(root,next.catalog,L".flac");next.ogg=OutputPath(root,next.catalog,L".ogg");next.aac=OutputPath(root,next.catalog,L".m4a");
    Write(next.flac,Audio(next.catalog));ResetCounts();
    assert(ProbeSavedSong(next,cache)==next.flac&&listings==1&&recording_opens==1);
    assert(DeleteFileW(WindowsPath(next.flac).c_str())&&DeleteFileW(WindowsPath(old).c_str()));
    ResetCounts();assert(ProbeSavedSong(next,cache).empty()&&listings==1);
    next.generation++;ResetCounts();assert(ProbeSavedSong(next,cache).empty()&&listings==1); // New settings bypass cooldown.
    next.generation++;deny_listing=true;ResetCounts();assert(ProbeSavedSong(next,cache).empty()&&listings==1);
    deny_listing=false;ResetCounts();assert(ProbeSavedSong(next,cache).empty()&&listings==0&&signature_checks==0);
    next.serial--;ResetCounts();assert(ProbeSavedSong(next,cache).empty()&&listings==0&&signature_checks==0);
    CloseHandle(destination_event);destination_event=nullptr;
    destination_enabled=false;
}
void PreferredPublication(const std::wstring& root) {
    const auto c=Song(L"Preferred publication");
    Media media;media.title=c.title;media.artist=media.album_artist=c.artist;media.album=c.album;
    media.track=c.track;media.duration=c.duration;media.playing=true;
    Heard heard;heard.media=media;heard.start=1;heard.finish=2;
    {ExclusiveSrw guard(history::lock);history::settings.aac=true;history::settings.metadata=false;}
    Capture incoming;incoming.aac=true; // Preferred recording short-circuits before muxing.
    for(const auto* extension:{L".flac",L".ogg"}) {
        const auto path=OutputPath(root,c,extension);
        Write(path,Audio(c,16,std::wstring(extension)==L".ogg"));
        ResetCounts();assert(Publish(incoming,heard,CaptureEpoch())==Publication::Skipped);
        assert(listings==1&&attribute_checks==0&&recording_opens==1);
        assert(DeleteFileW(WindowsPath(path).c_str()));
    }
}
void StableArtistFolder(const std::wstring& root) {
    Media media;media.title=L"Collaborative track";media.artist=media.album_artist=L"Artist";
    media.album=L"Album";media.track=3;media.duration=1;media.playing=true;
    const auto catalog=CatalogFor(media);
    assert(catalog.artist==L"Artist; Guest"&&catalog.folder_artist==L"Artist");
    auto original=catalog;original.artist=L"Artist";original.folder_artist.clear();
    const auto title=OutputPath(root,catalog,L".flac");
    assert(title==OutputPath(root,original,L".flac"));
    auto drifted=catalog;drifted.folder_artist.clear();
    const auto wrong_folder=OutputPath(root,drifted,L".flac");assert(wrong_folder!=title);
    const auto slash=title.find_last_of(L'\\');const auto old=title.substr(0,slash+1)+L"03 - "+title.substr(slash+1);
    Write(old,Audio(original));
    {ExclusiveSrw guard(history::lock);history::settings.root=root;history::settings.convert_numbered_filenames=true;
     history::settings.track_number_in_filename=false;history::settings.metadata=false;history::settings.generation=7;}
    FilenameCheck check{catalog,GetSettings(),filename_check_serial.load()};
    ResetCounts();ProcessFilenameCheck(check);assert(listings==2); // Each of the two exact artist folders is listed once.
    assert(GetFileAttributesW(WindowsPath(old).c_str())==INVALID_FILE_ATTRIBUTES);
    RecordingFile saved;assert(ReadRecording(title,saved)&&saved.complete&&MatchesRecording(saved,catalog));
    Tags identity;ApplyCatalogIdentity(identity,catalog);
    assert(std::find(identity.fields.begin(),identity.fields.end(),std::make_pair(std::string("ALBUMARTIST"),std::string("Artist; Guest")))!=identity.fields.end());
    Capture incoming;incoming.lossless=true;incoming.flac.rate=44100;incoming.flac.channels=2;incoming.flac.bits=16;
    Heard heard;heard.media=media;heard.start=1;heard.finish=2;
    assert(Publish(incoming,heard,CaptureEpoch())==Publication::Skipped);
    assert(GetFileAttributesW(WindowsPath(wrong_folder).c_str())==INVALID_FILE_ATTRIBUTES);
    // A recording already saved under enriched artist names is still found;
    // existing higher quality wins across both known folders without copying.
    const auto alternate_old=wrong_folder.substr(0,wrong_folder.find_last_of(L'\\')+1)+L"03 - "+wrong_folder.substr(wrong_folder.find_last_of(L'\\')+1);
    Write(alternate_old,Audio(catalog,24));
    ResetCounts();ProcessFilenameCheck(check);assert(listings==2);
    assert(GetFileAttributesW(WindowsPath(alternate_old).c_str())==INVALID_FILE_ATTRIBUTES);
    assert(ReadRecording(wrong_folder,saved)&&saved.quality.bits==24);
    assert(DeleteFileW(WindowsPath(title).c_str()));
    assert(Publish(incoming,heard,CaptureEpoch())==Publication::Skipped);
    assert(GetFileAttributesW(WindowsPath(title).c_str())==INVALID_FILE_ATTRIBUTES);
    assert(ReadRecording(wrong_folder,saved)&&saved.quality.bits==24);
    assert(DeleteFileW(WindowsPath(wrong_folder).c_str()));
}
}
namespace history {
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media& media,Catalog& c){c.disc=2;c.spotify_uri="spotify:track:"+Utf8(media.title);if(media.title==L"Collaborative track")c.artist=L"Artist; Guest";}
void EnrichTags(const Media&,Tags&) {}
}
int wmain(int argc,wchar_t** argv) {
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,root=exe.substr(0,exe.find_last_of(L'\\'))+L"\\Music-\u97f3\u697d";
    history::log_enabled=history::debug_enabled=0;
    if(argc==2&&std::wstring(argv[1])==L"--song-cache") {
        MenuLookup(root);StableArtistFolder(root);
        puts("PASS: verified Song cache avoids directory listings; misses back off; refresh bypasses cooldown; enriched artist tags retain stable folder lookup, prefix conversion and equal-quality reuse");
        return 0;
    }
    Lookups(root);Conversion(root);MenuLookup(root);PreferredPublication(root);StableArtistFolder(root);
    puts("PASS: one directory listing per lookup across formats; only listed recordings inspected; collision/best-quality reuse, casefold, recovery, blocked/partial/bounded enumeration, conversion and menu cache");
    return 0;
}
