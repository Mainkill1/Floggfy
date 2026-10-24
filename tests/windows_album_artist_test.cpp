// Actual metadata bridge, cache merge and Windows filename operations; no workers
// or live client/network requests are started by this focused executable.
#include "../native/metadata_bridge.cpp"
#include "../native/history_settings.cpp"
#include "../native/file_publication.h"
#include "recording_fixture.h"
#include <cassert>
namespace history {
void EnrichStoredMetadata(RichMetadata&) {}
void CacheAlbumArtwork(const std::string&,const std::string&) {}
bool GetAlbumArtwork(const std::string&,std::vector<uint8_t>&,std::string&) {return false;}
}
using namespace history;
static std::string Value(const Tags& tags,const char* key) {
    for(const auto& field:tags.fields)if(field.first==key)return field.second;
    return {};
}
int wmain() {
    RichMetadata live;live.uri="spotify:track:0000000000000000000001";live.title="Song";live.artist="Lead";live.album="Album";live.duration=200;
    live.fields={{"ARTIST","Lead"},{"ALBUMARTIST","Album Lead"}};
    CachedTrackMetadata stored;stored.uri=live.uri;stored.title=live.title;stored.album=live.album;stored.duration=live.duration;
    stored.fields={{"ARTIST","Lead; Guest"},{"ALBUMARTIST","Album Lead; Guest"}};
    assert(MergeCachedMetadata(stored,live));
    assert(live.fields.at("ARTIST")=="Lead; Guest"&&live.fields.at("ALBUMARTIST")=="Album Lead");
    live.fields["ALBUMARTIST"]="Album Lead; Co-Album Lead";
    assert(MergeCachedMetadata(stored,live)&&live.fields.at("ALBUMARTIST")=="Album Lead; Co-Album Lead");
    live.fields.erase("ALBUMARTIST");assert(MergeCachedMetadata(stored,live)&&live.fields.at("ALBUMARTIST")=="Album Lead; Guest");
    // Legacy expanded album credits must not replace a known media album artist
    // in either folder identity or emitted ALBUMARTIST tags.
    live.fields["ARTIST"]="Lead; Guest";live.fields["ALBUMARTIST"]="Album Lead; Guest";
    live.fields["TRACKNUMBER"]="1";live.fields["DISCNUMBER"]="1";
    {ExclusiveSrw guard(cache_lock);cache.Put(live);}
    Media media;media.title=L"Song";media.artist=L"Lead";media.album_artist=L"Album Lead";media.album=L"Album";media.duration=200;media.track=1;
    Catalog catalog;catalog.title=media.title;catalog.artist=media.album_artist;catalog.folder_artist=media.album_artist;catalog.album=media.album;catalog.duration=media.duration;catalog.track=1;
    EnrichCatalog(media,catalog);
    assert(catalog.artist==L"Album Lead"&&catalog.legacy_artist==L"Album Lead; Guest"&&catalog.spotify_uri==live.uri);
    Tags tags;EnrichTags(media,tags);
    assert(Value(tags,"ALBUMARTIST")=="Album Lead"&&Value(tags,"ARTIST")=="Lead; Guest");
    // Also retain the old contributing-artist folder when current album metadata
    // is correct and no longer contains those guests.
    live.fields["ALBUMARTIST"]="Album Lead";{ExclusiveSrw guard(cache_lock);cache.Put(live);}
    catalog.legacy_artist.clear();EnrichCatalog(media,catalog);
    assert(catalog.artist==L"Album Lead"&&catalog.legacy_artist==L"Lead; Guest");
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,root=exe.substr(0,exe.find_last_of(L'\\'))+L"\\Music";
    auto old=catalog;old.folder_artist.clear();old.artist=catalog.legacy_artist;old.legacy_artist.clear();
    const auto original=OutputPath(root,old,L".flac",false,true),target=OutputPath(root,old,L".flac");
    assert(EnsureDirectory(original.substr(0,original.find_last_of(L'\\'))));
    tags.fields.push_back({"SPOTIFY_URI",live.uri});tags.fields.push_back({"HISTORY_COMPLETE_LISTEN","1"});tags.fields.push_back({"HISTORY_CAPTURE","native compressed FLAC input"});
    std::vector<uint8_t> bytes;std::string error;assert(TagFlac(recording_fixture::fixture(),tags,bytes,error));
    HANDLE file=CreateFileW(WindowsPath(original).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
    DWORD written=0;assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());CloseHandle(file);
    const auto expected=OutputPath(root,catalog,L".flac");
    auto existing=ResolveRecordingDestination(expected,catalog);assert(existing.state==RecordingDestinationState::Existing&&existing.path==original);
    std::wstring renamed;assert(ConvertRecordingFilename(original,target,catalog,renamed)==FilenameConversion::Renamed&&renamed==target);
    existing=ResolveRecordingDestination(expected,catalog);assert(existing.state==RecordingDestinationState::Existing&&existing.path==target);
    assert(GetFileAttributesW(WindowsPath(expected).c_str())==INVALID_FILE_ATTRIBUTES); // No new album-folder copy.
    assert(DeleteFileW(WindowsPath(target).c_str()));
    // Metadata=0 remains disabled and leaves existing caller metadata untouched.
    {ExclusiveSrw guard(history::lock);history::settings.metadata=false;}
    Tags disabled;disabled.fields={{"ALBUMARTIST","Keep"}};EnrichTags(media,disabled);assert(Value(disabled,"ALBUMARTIST")=="Keep");
    puts("PASS: cached merge preserves album artist; production catalog/tags keep album artist and guests separate; old contributing folders are reused and renamed without a new copy; metadata opt-out retained");
    return 0;
}
