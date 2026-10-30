// One focused integration executable; only isolated Windows TEMP fixtures.
#include "../native/async_log.h"
namespace {unsigned in_use_warnings=0;}
namespace history {void ObservedLogEvent(LogKind,const char*,const std::string&);}
#define LogEvent ObservedLogEvent
#include "../native/audio_history.cpp"
#undef LogEvent
#include "../native/history_settings.cpp"
#include "recording_fixture.h"
#include "artwork_fixture.h"
#include <cassert>

namespace {
history::Tags refresh_tags;
std::wstring cancel_phase;std::string expected_refresh_uri;unsigned metadata_stages=0;
struct FixtureFile {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~FixtureFile(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
std::vector<uint8_t> ReadBytes(const std::wstring& path) {
    FixtureFile file{CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)};
    if(file.handle==INVALID_HANDLE_VALUE)std::fwprintf(stderr,L"Fixture read failed: %s error=%lu\n",path.c_str(),GetLastError());
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
std::vector<uint8_t> Tagged(const Catalog& c,const wchar_t* extension,bool owned=true,unsigned bits=16,bool sparse=false) {
    Tags tags;tags.fields={{"TITLE",Utf8(c.title)},{"ARTIST",Utf8(c.artist)},{"ALBUMARTIST",Utf8(c.artist)},
        {"ALBUM",Utf8(c.album)},{"TRACKNUMBER",std::to_string(c.track)},{"DISCNUMBER",std::to_string(c.disc)},
        {"SPOTIFY_URI",c.spotify_uri},{"HISTORY_MEDIA_DURATION","1"},{"HISTORY_COMPLETE_LISTEN","1"},
        {"CUSTOM","Preserve me"},{"LYRICS","[00:00.100]Unicode \xe9\x9f\xb3\xe6\xa5\xbd"}};
    if(sparse)tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="LYRICS";}),tags.fields.end());
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
std::vector<uint8_t> AudioPayload(const std::vector<uint8_t>& bytes,bool flac) {
    if(flac){FlacInfo info;size_t offset=0;assert(ParseFlacMetadata(bytes,info,offset)==FlacParse::Valid);return {bytes.begin()+offset,bytes.end()};}
    std::vector<uint8_t> audio,packet;unsigned index=0;
    for(size_t page=0;page<bytes.size();) {
        const size_t header=27+bytes[page+26];size_t body=page+header;
        for(size_t at=page+27;at<page+header;++at) {
            const size_t length=bytes[at];packet.insert(packet.end(),bytes.begin()+body,bytes.begin()+body+length);body+=length;
            if(length<255){if(index++!=1)audio.insert(audio.end(),packet.begin(),packet.end());packet.clear();}
        }
        page=body;
    }
    assert(packet.empty());return audio;
}
std::string TagValue(const RecordingFile& recording,const char* key) {
    for(const auto& field:recording.tags.fields)if(field.first==key)return field.second;
    return {};
}
void ExistingMetadataRefresh(const std::wstring& root) {
    SetNaming(false,false);
    auto preferences=[](bool enabled=true) {
        ExclusiveSrw guard(history::lock);auto& s=history::settings;
        s.downloads=true;s.metadata=true;s.update_existing_metadata=enabled;s.lyrics=true;s.flac=true;s.ogg=true;++s.generation;
    };
    for(bool flac:{true,false}) {
        preferences();auto media=MediaFor(flac?L"Early metadata FLAC":L"Early metadata Ogg");
        media.playing=false; // Verified Ready while paused; no completed listen.
        const auto catalog=CatalogFor(media);expected_refresh_uri=catalog.spotify_uri;const auto path=OutputPath(root,catalog,flac?L".flac":L".ogg");
        const auto original=Tagged(catalog,flac?L".flac":L".ogg",true,16,true);WriteBytes(path,original);
        refresh_tags.fields={{"GENRE","Fresh cached genre"}};
        // Players may allow reading while denying atomic replacement. Preserve
        // that distinction so a current track can retry after the player closes.
        HANDLE playing_file=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(playing_file!=INVALID_HANDLE_VALUE);
        assert(UpdateRecordingMetadata(path,refresh_tags,catalog)==FileMetadataUpdate::InUse);
        assert(ReadBytes(path)==original);
        PublishCurrentDestination(media,GetSettings());auto check=TakeFilenameCheck();
        assert(check); // Conversion is off: metadata refresh independently schedules.
        const auto warnings_before=in_use_warnings;
        ProcessFilenameCheck(*check);
        {ExclusiveSrw guard(filename_check_lock);
         assert(pending_filename_check&&pending_filename_check->warned_in_use&&pending_filename_check->retry_at>GetTickCount64());}
        for(unsigned i=0;i<10;++i){PublishCurrentDestination(media,GetSettings());assert(!TakeFilenameCheck());}
        SaveJob pending_audio;assert(!saves.Pop(pending_audio)&&ReadBytes(path)==original);
        {ExclusiveSrw guard(filename_check_lock);pending_filename_check->retry_at=0;}
        check=TakeFilenameCheck();assert(check&&check->warned_in_use);ProcessFilenameCheck(*check);
        assert(in_use_warnings==warnings_before+1); // The retry is a wait, not another warning event.
        assert(CloseHandle(playing_file));
        // Byte-range locks can reject ReadFile after the delete-capable open
        // succeeds; preserve the same typed deferral and exact source bytes.
        HANDLE range_file=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        assert(range_file!=INVALID_HANDLE_VALUE);OVERLAPPED range{};
        assert(LockFileEx(range_file,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range));
        assert(UpdateRecordingMetadata(path,refresh_tags,catalog)==FileMetadataUpdate::InUse);
        assert(UnlockFileEx(range_file,0,1,0,&range)&&CloseHandle(range_file));
        // Advance only the retry clock, never freshness/identity authorization.
        {ExclusiveSrw guard(filename_check_lock);pending_filename_check->retry_at=0;}
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check&&check->warned_in_use);
        ProcessFilenameCheck(*check);RecordingFile result;assert(ReadRecording(path,result)&&result.complete&&result.owned);
        assert(TagValue(result,"GENRE")=="Fresh cached genre"&&TagValue(result,"CUSTOM")=="Preserve me");
        auto updated=ReadBytes(path);assert(updated!=original&&AudioPayload(updated,flac)==AudioPayload(original,flac));
        for(unsigned i=0;i<10;++i){PublishCurrentDestination(media,GetSettings());assert(!TakeFilenameCheck());}
        SaveJob job;assert(!saves.Pop(job)); // No capture/audio publication job was created.
        // A later cache revision delivers synchronized lyrics and a larger client cover.
        refresh_tags.fields={{"LYRICS","[00:01.000]Arrived after pause"},{"FLOGGFY_ARTWORK_SOURCE","spotify-client"}};
        refresh_tags.cover=artwork_fixture::jpeg();refresh_tags.mime="image/jpeg";MetadataChanged();
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadRecording(path,result)&&TagValue(result,"LYRICS")=="[00:01.000]Arrived after pause");
        assert(result.artwork.width==3&&result.artwork.height==2);
        updated=ReadBytes(path);assert(AudioPayload(updated,flac)==AudioPayload(original,flac));
        const auto stages=metadata_stages;MetadataChanged(); // Unrelated cache changes may inspect, never rewrite.
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadBytes(path)==updated&&metadata_stages==stages);
        // Each opt-out revokes an already queued refresh, including format and lyrics.
        refresh_tags={};refresh_tags.fields={{"LABEL","Later label"},{"LYRICS","[00:01.000]Replacement must not erase old synced lyrics"}};
        for(unsigned option=0;option<5;++option) {
            preferences();PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);
            {ExclusiveSrw guard(history::lock);auto& s=history::settings;
             if(option==0)s.downloads=false;else if(option==1)s.metadata=false;else if(option==2)s.update_existing_metadata=false;
             else if(option==3){if(flac)s.flac=false;else s.ogg=false;}else s.lyrics=false;
             ++s.generation;}
            ProcessFilenameCheck(*check);assert(ReadBytes(path)==updated);
        }
        // A current intent with Lyrics=0 still fills other details and preserves old lyrics/art.
        preferences();{ExclusiveSrw guard(history::lock);history::settings.lyrics=false;++history::settings.generation;}
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadRecording(path,result)&&TagValue(result,"LABEL")=="Later label"&&TagValue(result,"LYRICS")=="[00:01.000]Arrived after pause");
        updated=ReadBytes(path);assert(AudioPayload(updated,flac)==AudioPayload(original,flac));
        preferences();refresh_tags.fields={{"COMMENT","Cancelled detail"}};
        // Superseding identity, expired Ready observation and explicit cancellation retain the exact file.
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);
        playing_file=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(playing_file!=INVALID_HANDLE_VALUE);ProcessFilenameCheck(*check);
        {ExclusiveSrw guard(filename_check_lock);assert(pending_filename_check);check=*pending_filename_check;}
        PublishCurrentDestination(MediaFor(L"Other identity"),GetSettings());ProcessFilenameCheck(*check);assert(ReadBytes(path)==updated);
        auto superseding=TakeFilenameCheck();assert(superseding&&superseding->serial!=check->serial);
        assert(CloseHandle(playing_file));
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);
        filename_check_valid_until.store(0);ProcessFilenameCheck(*check);assert(ReadBytes(path)==updated);
        CancelFilenameChecks();PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);
        CancelFilenameChecks();ProcessFilenameCheck(*check);assert(ReadBytes(path)==updated);
        // Revoke during staging and after exact-object reservation: source rollback must remain byte-identical.
        for(const wchar_t* phase:{L"staged",L"reserved"}) {
            PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);
            cancel_phase=phase;ProcessFilenameCheck(*check);assert(cancel_phase.empty()&&ReadBytes(path)==updated);
        }
        for(unsigned throw_at:{1u,3u,4u}) {
            struct Throwing {unsigned calls=0,at;} throwing{0,throw_at};
            auto permit=[](void* argument) {auto& t=*static_cast<Throwing*>(argument);if(++t.calls==t.at)throw 7;return true;};
            bool escaped=false;FileMetadataUpdate result=FileMetadataUpdate::Failed;
            try{result=UpdateRecordingMetadata(path,refresh_tags,catalog,GetSettings().generation,permit,&throwing);}catch(...){escaped=true;}
            assert(!escaped&&result==FileMetadataUpdate::Skipped&&ReadBytes(path)==updated);
            assert(MissingFile(path+L".previous"));
            WIN32_FIND_DATAW data{};HANDLE residue=FindFirstFileW(WindowsPath(path+L".complete-lyrics-*").c_str(),&data);
            assert(residue==INVALID_HANDLE_VALUE); // Throwing permits leave neither staging nor backup residue.
        }
        assert(!saves.Pop(job)&&DeleteFileW(WindowsPath(path).c_str()));CancelFilenameChecks();
        // An owned but different URI at the same pathname is never updated.
        auto wrong=catalog;wrong.spotify_uri+="-different";const auto foreign=Tagged(wrong,flac?L".flac":L".ogg",true,16,true);WriteBytes(path,foreign);
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadBytes(path)==foreign&&DeleteFileW(WindowsPath(path).c_str()));
        CancelFilenameChecks();const auto unowned=Tagged(catalog,flac?L".flac":L".ogg",false,16,true);WriteBytes(path,unowned);
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadBytes(path)==unowned&&DeleteFileW(WindowsPath(path).c_str()));
        CancelFilenameChecks();auto incomplete=original;incomplete.pop_back();WriteBytes(path,incomplete);
        PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
        assert(ReadBytes(path)==incomplete&&DeleteFileW(WindowsPath(path).c_str()));
    }
    // A delayed lock retry cannot use filename conversion to bypass freshness.
    expected_refresh_uri.clear();preferences();SetNaming(false,true);
    auto stale_media=MediaFor(L"Stale conversion retry");const auto stale_catalog=CatalogFor(stale_media);
    const auto stale_title=OutputPath(root,stale_catalog,L".flac"),stale_old=Numbered(stale_title);
    const auto stale_bytes=Tagged(stale_catalog,L".flac",true,16,true);WriteBytes(stale_old,stale_bytes);
    refresh_tags={};refresh_tags.fields={{"GENRE","Must not rename stale retry"}};
    HANDLE stale_lock=CreateFileW(WindowsPath(stale_old).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(stale_lock!=INVALID_HANDLE_VALUE);PublishCurrentDestination(stale_media,GetSettings());auto stale_check=TakeFilenameCheck();assert(stale_check);
    ProcessFilenameCheck(*stale_check);
    {ExclusiveSrw guard(filename_check_lock);assert(pending_filename_check);pending_filename_check->retry_at=0;}
    assert(CloseHandle(stale_lock));filename_check_valid_until.store(0);
    stale_check=TakeFilenameCheck();assert(stale_check&&stale_check->warned_in_use);ProcessFilenameCheck(*stale_check);
    assert(MissingFile(stale_title)&&ReadBytes(stale_old)==stale_bytes&&!TakeFilenameCheck()&&DeleteFileW(WindowsPath(stale_old).c_str()));
    CancelFilenameChecks();
    expected_refresh_uri.clear();preferences();SetNaming(false,true);auto media=MediaFor(L"Enabled Ogg under disabled FLAC");const auto catalog=CatalogFor(media);
    const auto flac_path=OutputPath(root,catalog,L".flac"),ogg_path=OutputPath(root,catalog,L".ogg");
    const auto flac_bytes=Tagged(catalog,L".flac",true,16,true);WriteBytes(flac_path,flac_bytes);WriteBytes(ogg_path,Tagged(catalog,L".ogg",true,16,true));
    {ExclusiveSrw guard(history::lock);history::settings.flac=false;++history::settings.generation;}
    refresh_tags={};refresh_tags.fields={{"GENRE","Enabled Ogg only"}};
    PublishCurrentDestination(media,GetSettings());auto check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
    RecordingFile result;assert(ReadRecording(ogg_path,result)&&TagValue(result,"GENRE")=="Enabled Ogg only");
    assert(ReadBytes(flac_path)==flac_bytes&&DeleteFileW(WindowsPath(flac_path).c_str())&&DeleteFileW(WindowsPath(ogg_path).c_str()));
    refresh_tags={};expected_refresh_uri.clear();preferences(false);CancelFilenameChecks();
    puts("PASS: paused existing FLAC/Ogg refresh before full listen; late lyrics/art, no-op coalescing, opt-outs/stale/reserved cancellation, identity and exact audio preservation");
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
void ObservedLogEvent(LogKind kind,const char* event,const std::string& detail) {
    if(kind==LogKind::Warning&&detail.rfind("metadata update deferred:",0)==0)++in_use_warnings;
    LogEvent(kind,event,detail);
}
std::string ReadClientPlaybackQuality(const Media&){return {};}
void EnrichCatalog(const Media& media,Catalog& c){c.disc=2;c.spotify_uri=media.title==L"Raw/A"||media.title==L"Raw:A"?"":"spotify:track:"+Utf8(media.title);}
void EnrichTags(const Media& media,Tags& tags) {
    if(!expected_refresh_uri.empty())assert(media.spotify_uri==expected_refresh_uri);
    for(const auto& field:refresh_tags.fields)if(field.first!="LYRICS"||GetSettings().lyrics)tags.fields.push_back(field);
    tags.cover=refresh_tags.cover;tags.mime=refresh_tags.mime;
}
void PublicationTestPhase(const wchar_t* phase,const std::wstring&) {
    if(std::wstring(phase)==L"staged")++metadata_stages;
    if(cancel_phase==phase){cancel_phase.clear();CancelFilenameChecks();}
}
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
    DirectConversions(root);TrackStartChecks(root);ExistingMetadataRefresh(root);PublicationModes(root);
    assert(FlushSettings(5000));CloseHandle(save_event);save_event=nullptr;
    puts("PASS: naming defaults/conflict persistence; FLAC/Ogg/M4A atomic prefix removal preserves exact bytes; track-start coalescing/cancellation; audio queue independence; mode-aware publication, menu and recent targets");
    return 0;
}
