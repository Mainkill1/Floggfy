// One focused integration executable; only isolated Windows TEMP fixtures.
#include "../native/audio_history.cpp"
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <cassert>

namespace {
struct FixtureFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~FixtureFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
std::vector<uint8_t> ReadBytes(const std::wstring& path) {
    FixtureFile file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)};
    assert(file.handle!=INVALID_HANDLE_VALUE);const auto size=GetFileSize(file.handle,nullptr);assert(size!=INVALID_FILE_SIZE);
    std::vector<uint8_t> bytes(size);DWORD read=0;
    assert(ReadFile(file.handle,bytes.data(),size,&read,nullptr)&&read==size);return bytes;
}
void WriteBytes(const std::wstring& path,const std::vector<uint8_t>& bytes) {
    assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
    FixtureFile file{CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr)};
    assert(file.handle!=INVALID_HANDLE_VALUE);DWORD written=0;
    assert(WriteFile(file.handle,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());
}
bool MissingFile(const std::wstring& path){return GetFileAttributesW(WindowsPath(path).c_str())==INVALID_FILE_ATTRIBUTES;}
std::wstring Numbered(const std::wstring& title_path) {
    const auto slash=title_path.find_last_of(L'\\');return title_path.substr(0,slash+1)+L"03 - "+title_path.substr(slash+1);
}
Catalog Identity(const std::wstring& title) {
    Catalog c;c.title=title;c.artist=L"Artist";c.album=L"Album";c.track=3;c.disc=2;c.duration=1;
    c.spotify_uri="spotify:track:"+Utf8(title);return c;
}
std::vector<uint8_t> Tagged(const Catalog& c,const wchar_t* extension,bool owned=true,unsigned bits=16) {
    Tags tags;tags.fields={{"TITLE",Utf8(c.title)},{"ARTIST",Utf8(c.artist)},{"ALBUMARTIST",Utf8(c.artist)},
        {"ALBUM",Utf8(c.album)},{"TRACKNUMBER",std::to_string(c.track)},{"DISCNUMBER",std::to_string(c.disc)},
        {"SPOTIFY_URI",c.spotify_uri},{"HISTORY_MEDIA_DURATION","1"},{"HISTORY_COMPLETE_LISTEN","1"},
        {"CUSTOM","Preserve me"},{"LYRICS","[00:00.100]Unicode \xe9\x9f\xb3\xe6\xa5\xbd"}};
    const std::wstring ext=extension;
    if(owned)tags.fields.emplace_back("HISTORY_CAPTURE",ext==L".flac"?"native compressed FLAC input":
        ext==L".ogg"?"native compressed Ogg pages":"native compressed SpAC AAC packets");
    tags.cover=artwork_fixture::png();tags.mime="image/png";
    std::vector<uint8_t> bytes;std::string error;
    if(ext==L".flac")assert(TagFlac(recording_fixture::fixture(44100,bits),tags,bytes,error));
    else if(ext==L".ogg")assert(TagOgg(recording_fixture::ogg(),tags,bytes,error));
    else {AacInfo info{44100,2,2048,100,2500,{0xeb,0x8a,0x08,0x00}};
        assert(MakeAacM4a(info,{{0x21,0x10,0x04},{0x21,0x10,0x04}},tags,bytes,error));}
    return bytes;
}
void DirectConversions(const std::wstring& root) {
    for(const auto* extension:{L".flac",L".ogg",L".m4a"}) {
        const auto c=Identity(L"Rename \u97f3\u697d "+std::wstring(extension));
        const auto title=OutputPath(root,c,extension,false,false),old=Numbered(title);
        const auto bytes=Tagged(c,extension);WriteBytes(old,bytes);
        std::wstring renamed=L"unchanged";
        assert(ConvertRecordingFilename(old,title,c,renamed)==FilenameConversion::Renamed);
        assert(renamed==title&&MissingFile(old)&&ReadBytes(title)==bytes);
        renamed=L"sentinel";
        assert(ConvertRecordingFilename(title,title,c,renamed)==FilenameConversion::Unchanged&&renamed==L"sentinel");
        assert(ReadBytes(title)==bytes&&DeleteFileW(WindowsPath(title).c_str()));
        const auto slash=old.find_last_of(L'\\');
        const auto disc=old.substr(0,slash+1)+L"Disc 2 - "+old.substr(slash+1);
        WriteBytes(disc,bytes);
        assert(ConvertRecordingFilename(disc,title,c,renamed)==FilenameConversion::Renamed&&renamed==title);
        assert(MissingFile(disc)&&ReadBytes(title)==bytes&&DeleteFileW(WindowsPath(title).c_str()));
        const auto suffix=old.substr(0,old.size()-std::wstring(extension).size())+RecordingSuffix(c)+extension;
        const auto target=title.substr(0,title.size()-std::wstring(extension).size())+RecordingSuffix(c)+extension;
        WriteBytes(suffix,bytes);
        assert(ConvertRecordingFilename(suffix,title,c,renamed)==FilenameConversion::Renamed&&renamed==target);
        assert(MissingFile(suffix)&&ReadBytes(target)==bytes&&DeleteFileW(WindowsPath(target).c_str()));

        WriteBytes(old,bytes);auto different=c;different.spotify_uri+="different";
        const auto occupied=Tagged(different,extension);WriteBytes(title,occupied);
        assert(ConvertRecordingFilename(old,title,c,renamed)==FilenameConversion::Unchanged);
        assert(ReadBytes(old)==bytes&&ReadBytes(title)==occupied&&DeleteFileW(WindowsPath(title).c_str()));
        unsigned permits=0;
        auto revoke=[](void* context){return ++*static_cast<unsigned*>(context)==1;};
        assert(ConvertRecordingFilename(old,title,c,renamed,revoke,&permits)==FilenameConversion::Unchanged);
        assert(permits==2&&!MissingFile(old)&&MissingFile(title)&&ReadBytes(old)==bytes);
        auto wrong=c;wrong.spotify_uri+="wrong-recording";
        assert(ConvertRecordingFilename(old,title,wrong,renamed)==FilenameConversion::Failed&&ReadBytes(old)==bytes);
        {FixtureFile lock{CreateFileW(WindowsPath(old).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)};
         assert(lock.handle!=INVALID_HANDLE_VALUE);
         assert(ConvertRecordingFilename(old,title,c,renamed)==FilenameConversion::Failed&&MissingFile(title));}
        assert(ReadBytes(old)==bytes&&DeleteFileW(WindowsPath(old).c_str()));
        WriteBytes(old,Tagged(c,extension,false));
        assert(ConvertRecordingFilename(old,title,c,renamed)==FilenameConversion::Failed&&MissingFile(title));
        assert(DeleteFileW(WindowsPath(old).c_str()));
    }
    auto c=Identity(L"03 - Actual song title");const auto title=OutputPath(root,c,L".flac");
    const auto bytes=Tagged(c,L".flac");WriteBytes(title,bytes);std::wstring renamed;
    assert(ConvertRecordingFilename(title,title,c,renamed)==FilenameConversion::Unchanged&&ReadBytes(title)==bytes);
    assert(DeleteFileW(WindowsPath(title).c_str()));
}
void SetNaming(bool numbering,bool conversion) {
    const auto epoch=CaptureEpoch();
    {ExclusiveSrw guard(history::lock);history::settings.track_number_in_filename=numbering;
     history::settings.convert_numbered_filenames=conversion;++history::settings.generation;}
    SetEvent(history::settings_signal);assert(FlushSettings(5000)&&CaptureEpoch()==epoch);
}
Media MediaFor(const std::wstring& title) {
    Media m;m.title=title;m.artist=m.album_artist=L"Artist";m.album=L"Album";m.track=3;m.duration=1;m.playing=true;return m;
}
void TrackStartChecks(const std::wstring& root) {
    SetNaming(false,false);auto media=MediaFor(L"Track start A");auto c=CatalogFor(media);
    const auto title=OutputPath(root,c,L".flac"),old=Numbered(title);const auto bytes=Tagged(c,L".flac");WriteBytes(old,bytes);
    PublishCurrentDestination(media,GetSettings());assert(!TakeFilenameCheck()&&!MissingFile(old)&&MissingFile(title));
    SetNaming(false,true);RecordRecentDownload(L"Existing row",old,bytes.size());
    PublishCurrentDestination(media,GetSettings());auto first=TakeFilenameCheck();assert(first);
    // A superseding track cancels the old request before any rename.
    auto next=MediaFor(L"Track start B");const auto next_c=CatalogFor(next);
    const auto next_title=OutputPath(root,next_c,L".flac"),next_old=Numbered(next_title);
    const auto next_bytes=Tagged(next_c,L".flac");WriteBytes(next_old,next_bytes);
    AcquireSRWLockExclusive(&filename_check_lock);
    PublishCurrentDestination(next,GetSettings()); // Lost try-lock must still cancel A.
    ReleaseSRWLockExclusive(&filename_check_lock);
    ProcessFilenameCheck(*first);
    assert(!MissingFile(old)&&MissingFile(title));
    assert(!TakeFilenameCheck());
    PublishCurrentDestination(next,GetSettings()); // Same B intent retries submission.
    auto pending=TakeFilenameCheck();assert(pending);ProcessFilenameCheck(*pending);
    assert(MissingFile(next_old)&&ReadBytes(next_title)==next_bytes);
    PublishCurrentDestination(next,GetSettings());assert(!TakeFilenameCheck()); // No heartbeat disk loop.
    PublishCurrentDestination(media,GetSettings());pending=TakeFilenameCheck();assert(pending);ProcessFilenameCheck(*pending);
    assert(MissingFile(old)&&ReadBytes(title)==bytes); // No completed listen/capture was needed.
    std::vector<RecentDownload> rows;assert(ReadRecentDownloads(rows)&&rows.size()==1&&rows[0].path==title&&rows[0].bytes==bytes.size());
    assert(DeleteFileW(WindowsPath(title).c_str())&&DeleteFileW(WindowsPath(next_title).c_str()));

    media=MediaFor(L"Queue check");c=CatalogFor(media);const auto queue_title=OutputPath(root,c,L".flac"),queue_old=Numbered(queue_title);
    WriteBytes(queue_old,Tagged(c,L".flac"));SaveJob one,two;assert(saves.Push(std::move(one))&&saves.Push(std::move(two)));
    PublishCurrentDestination(media,GetSettings());pending=TakeFilenameCheck();assert(pending);
    SaveJob retained;assert(saves.Pop(retained)&&saves.Pop(retained)&&!saves.Pop(retained)); // Check uses no audio slots.
    CancelFilenameChecks();ProcessFilenameCheck(*pending);assert(!MissingFile(queue_old)&&MissingFile(queue_title));
    PublishCurrentDestination(media,GetSettings());pending=TakeFilenameCheck();assert(pending);ProcessFilenameCheck(*pending);
    assert(MissingFile(queue_old)&&!MissingFile(queue_title)&&DeleteFileW(WindowsPath(queue_title).c_str()));
    auto raw_a=MediaFor(L"Raw/A"),raw_b=MediaFor(L"Raw:A");
    assert(OutputPath(root,CatalogFor(raw_a),L".flac")==OutputPath(root,CatalogFor(raw_b),L".flac"));
    PublishCurrentDestination(raw_a,GetSettings());auto raw_first=TakeFilenameCheck();assert(raw_first&&raw_first->catalog.spotify_uri.empty());
    PublishCurrentDestination(raw_b,GetSettings());auto raw_second=TakeFilenameCheck();assert(raw_second&&raw_second->catalog.title==raw_b.title);
    assert(raw_first->serial!=raw_second->serial);CancelFilenameChecks();

    const auto late=Identity(L"Late settings cancellation");
    const auto late_title=OutputPath(root,late,L".flac"),late_old=Numbered(late_title);
    const auto late_bytes=Tagged(late,L".flac");WriteBytes(late_old,late_bytes);
    struct RevokeSettings {FilenamePermit permit;unsigned calls=0;} revocation{{GetSettings().generation,0},0};
    auto settings_revoke=[](void* context) {
        auto& gate=*static_cast<RevokeSettings*>(context);
        if(++gate.calls==2)assert(SetDownloads(false));
        return FilenameStillCurrent(&gate.permit);
    };
    std::wstring moved;
    assert(ConvertRecordingFilename(late_old,late_title,late,moved,settings_revoke,&revocation)==FilenameConversion::Unchanged);
    assert(revocation.calls==2&&MissingFile(late_title)&&ReadBytes(late_old)==late_bytes);
    assert(SetDownloads(true)&&FlushSettings(5000)&&DeleteFileW(WindowsPath(late_old).c_str()));
}
void PublicationModes(const std::wstring& root) {
    auto media=MediaFor(L"Numbered new recording");const auto c=CatalogFor(media);
    const auto title=OutputPath(root,c,L".flac",false,false),numbered=OutputPath(root,c,L".flac",false,true);
    assert(numbered==Numbered(title));
    auto publish=[&] {
        const auto source=recording_fixture::fixture();Capture capture;capture.lossless=true;capture.bytes=source.size();
        size_t offset=0;assert(ParseFlacMetadata(source,capture.flac,offset)==FlacParse::Valid);
        capture.coverage.samples=16;assert(capture.data.Append(source.data(),source.size(),1024*1024));
        Heard heard;heard.media=media;heard.start=1;heard.finish=2;
        return Publish(capture,heard,CaptureEpoch());
    };
    SetNaming(true,false);assert(publish()==Publication::Saved&&MissingFile(title)&&!MissingFile(numbered));
    const auto bytes=ReadBytes(numbered);
    SetNaming(false,false);assert(publish()==Publication::Skipped&&ReadBytes(numbered)==bytes&&MissingFile(title));
    SetNaming(false,true);assert(publish()==Publication::Skipped&&ReadBytes(title)==bytes&&MissingFile(numbered));
    SetNaming(true,false);assert(publish()==Publication::Skipped&&ReadBytes(title)==bytes&&MissingFile(numbered)); // No duplicate on mode switch.
    StartSongDestinationWorker(true);PublishSongDestination(777,root,c,false,GetSettings().generation,true);
    std::wstring menu_path;const auto end=GetTickCount64()+5000;
    while(!ReadSongDestination(777,GetSettings().generation,menu_path)){assert(GetTickCount64()<end);Sleep(1);}
    assert(menu_path==title);StartSongDestinationWorker(false);
    assert(DeleteFileW(WindowsPath(title).c_str()));
}
}
namespace history {
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media& media,Catalog& c){c.disc=2;c.spotify_uri=media.title==L"Raw/A"||media.title==L"Raw:A"?"":"spotify:track:"+Utf8(media.title);}
void EnrichTags(const Media&,Tags&) {}
}
int wmain() {
    assert(!Settings{}.track_number_in_filename&&!Settings{}.convert_numbered_filenames);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,directory=exe.substr(0,exe.find_last_of(L'\\'));
    const auto ini=directory+L"\\SpotifyHistory.ini",root=directory+L"\\Music-\u97f3\u697d";
    assert(CreateDefaultIni(ini));
    assert(GetPrivateProfileIntW(L"To Disk",L"TrackNumberInFilename",9,ini.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"ConvertNumberedFilenames",9,ini.c_str())==0);
    assert(WritePrivateProfileStringW(L"To Disk",L"TrackNumberInFilename",nullptr,ini.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"ConvertNumberedFilenames",nullptr,ini.c_str()));
    Settings missing;assert(ReadFilenameOptions(missing,ini)&&!missing.track_number_in_filename&&!missing.convert_numbered_filenames);
    for(unsigned bits=0;bits<4;++bits) {
        assert(WritePrivateProfileStringW(L"To Disk",L"TrackNumberInFilename",bits&1?L"1":L"0",ini.c_str()));
        assert(WritePrivateProfileStringW(L"To Disk",L"ConvertNumberedFilenames",bits&2?L"1":L"0",ini.c_str()));
        Settings parsed;parsed.capture_epoch=77;assert(ReadFilenameOptions(parsed,ini)==(bits==3));
        assert(parsed.track_number_in_filename==bool(bits&1)&&parsed.convert_numbered_filenames==(bits==2)&&parsed.capture_epoch==77);
    }
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",ini.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Save Location",root.c_str(),ini.c_str()));
    for(const auto* key:{L"Menu",L"Log",L"UpdateExistingMetadata"})assert(WritePrivateProfileStringW(L"To Disk",key,L"0",ini.c_str()));
    InitSettings(nullptr);assert(GetSettings().track_number_in_filename&&!GetSettings().convert_numbered_filenames&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"ConvertNumberedFilenames",9,ini.c_str())==0);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,16,ini.c_str())==2&&std::wstring(kept)==L"\u97f3\u697d");
    assert(GetSettings().root==root);
    save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(save_event);
    DirectConversions(root);TrackStartChecks(root);PublicationModes(root);
    assert(FlushSettings(5000));CloseHandle(save_event);save_event=nullptr;
    puts("PASS: naming defaults/conflict persistence; FLAC/Ogg/M4A atomic prefix removal preserves exact bytes; track-start coalescing/cancellation; audio queue independence; mode-aware publication, menu and recent targets");
    return 0;
}
