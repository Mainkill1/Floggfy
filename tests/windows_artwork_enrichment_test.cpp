// Exercises production enrichment with an offline artwork-cache boundary.
#include "../native/metadata_bridge.cpp"
#include "../native/history_settings.cpp"
#include "../native/file_publication.h"
#include "../native/flac_history_core.h"
#include "../native/metadata_update_windows.h"
#include "../native/recent_downloads.h"
#include "recording_fixture.h"
#include "../native/album_artwork.h"
#include "artwork_fixture.h"
#include <cassert>
#include <algorithm>
namespace {
std::string ready_uri;
std::vector<uint8_t> ready_cover;
std::string ready_mime;
unsigned artwork_reads=0;
}
namespace history {
std::uint64_t AlbumArtworkRevision() noexcept {return 0;}
bool CacheAlbumArtwork(const std::string&,const std::string&) {return false;}
bool IsAlbumArtworkUrl(const std::string& url){return url.rfind("https://i.scdn.co/image/",0)==0;}
bool DownloadAlbumArtwork(const std::string&,std::vector<uint8_t>&){return false;}
bool GetAlbumArtwork(const std::string& uri,std::vector<uint8_t>& bytes,std::string& mime) {
 ++artwork_reads;
 if(uri!=ready_uri || ready_cover.empty())return false;
 bytes=ready_cover;mime=ready_mime;return true;
}
}
static std::string ArtworkSource(const history::Tags& tags) {
 const auto found=std::find_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="FLOGGFY_ARTWORK_SOURCE";});
 return found==tags.fields.end()?std::string():found->second;
}
static void CheckClientArtwork() {
 history::RichMetadata record;record.title="Song";record.artist="Artist";record.album="Album";
 record.uri="spotify:track:0000000000000000000001";record.duration=200;
 record.fields["FLOGGFY_ARTWORK_SOURCE"]="untrusted text marker";
 history::cache.Put(record);
 history::Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;
 ready_uri=record.uri;
 history::Tags existing_tags;existing_tags.cover=artwork_fixture::png();existing_tags.mime="image/png";
 const auto embedded_cover=existing_tags.cover;
 history::EnrichTags(media,existing_tags);
 assert(existing_tags.cover==embedded_cover&&ArtworkSource(existing_tags).empty()); // No ready client image.
 ready_cover={1,2,3};history::EnrichTags(media,existing_tags);
 assert(existing_tags.cover==embedded_cover&&ArtworkSource(existing_tags).empty()); // Invalid client image.
 ready_cover=artwork_fixture::jpeg();ready_mime="image/jpeg";
 history::Tags tags;tags.cover=artwork_fixture::png();tags.mime="image/png";
 history::EnrichTags(media,tags);
 assert(tags.cover==ready_cover && tags.mime==ready_mime && "validated client artwork is selected");
 assert(ArtworkSource(tags)=="spotify-client");
 tags.fields.push_back({"FLOGGFY_ARTWORK_SOURCE","stale"});
 ready_cover=artwork_fixture::png();ready_mime="image/png";
 history::EnrichTags(media,tags);
 assert(tags.cover==ready_cover && tags.mime=="image/png" && "client image selection is independent of existing-file preservation policy");
 assert(ArtworkSource(tags)=="spotify-client"&&std::count_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="FLOGGFY_ARTWORK_SOURCE";})==1);
 const auto original=tags.cover;
 ready_cover={1,2,3};
 history::EnrichTags(media,tags);
 assert(tags.cover==original && tags.mime=="image/png");
 ready_cover=artwork_fixture::jpeg();ready_mime="image/jpeg";tags.cover.clear();tags.mime.clear();
 history::EnrichTags(media,tags);assert(tags.cover==ready_cover);
 const auto reads=artwork_reads;media.artist=L"Other";tags.cover=artwork_fixture::png();
 const auto retained=tags.cover;history::Tags unmatched;unmatched.cover=retained;unmatched.mime="image/png";history::EnrichTags(media,unmatched);
 assert(unmatched.cover==retained && ArtworkSource(unmatched).empty() && artwork_reads==reads && "unmatched track must not receive another track's cover");

}

static void CheckOwnedMetadata() {
 using namespace history;
 RichMetadata first;first.uri="spotify:episode:0000000000000000000009";first.title="Episode";
 first.artist="Publisher";first.album="Show";first.duration=200;
 first.fields["DATE"]="2025-09-10";
 first.artwork_url="https://i.scdn.co/image/"+std::string(40,'a');
 first.artwork_urls={first.artwork_url};
 auto artwork=std::make_shared<MetadataArtwork>();artwork->reference=first.artwork_url;
 artwork->mime="image/png";artwork->bytes=artwork_fixture::png();first.artwork=artwork;
 first.playback_quality="lossless";first.quality_time=42;
 RichMetadata sparse;sparse.uri=first.uri;sparse.title=first.title;sparse.duration=first.duration;
 auto unknown=sparse;MergeAvailableFields(first,unknown);assert(unknown.playback_quality.empty());
 sparse.metadata_source="online";
 MergeAvailableFields(first,sparse);
 assert(sparse.album=="Show"&&sparse.artist=="Publisher"&&sparse.fields.at("DATE")=="2025-09-10");
 assert(sparse.artwork_url==first.artwork_url&&sparse.artwork==artwork);
 assert(sparse.playback_quality=="lossless"&&sparse.quality_time==42);
 Media media;media.spotify_uri=first.uri;media.title=L"Episode";media.duration=200;
 cache.Put(sparse);media.metadata=cache.Pin(sparse);
 for(int i=0;i<80;++i){auto other=first;other.uri="spotify:episode:other"+std::to_string(i);cache.Put(other);}
 settings.fetch_missing_artwork=false;
 Tags tags;const auto reads=artwork_reads;EnrichTags(media,tags);
 settings.fetch_missing_artwork=true;
 assert(tags.cover==artwork->bytes&&artwork_reads==reads&&ArtworkSource(tags)=="spotify-client");
 auto corrected=sparse;corrected.artwork.reset();corrected.artwork_url="https://i.scdn.co/image/"+std::string(40,'b');corrected.artwork_urls={corrected.artwork_url};
 MergeAvailableFields(first,corrected);assert(!corrected.artwork);
 auto late=first;late.metadata_source="online";late.fields["LYRICS"]="later words";
 MergeSnapshot(corrected,late);
 assert(late.artwork_url==corrected.artwork_url&&!late.artwork&&late.fields.at("LYRICS")=="later words"); // Explicitly changed reference cannot reuse stale bytes.
 RichMetadata track=first;track.uri="spotify:track:0000000000000000000008";
 track.album_uri="spotify:album:0000000000000000000007";track.fields["ISRC"]="test";track.fields["LABEL"]="Label";
 assert(OnlineNeeds(track,false)==0&&OnlineNeeds(track,true)==4);
 track.album_uri.clear();track.fields.erase("LABEL");assert((OnlineNeeds(track,false)&3)==3); // Missing album ID authorizes its prerequisite.
 const auto owner=cache.Pin(track);
 settings.fetch_missing_metadata=false;settings.fetch_missing_lyrics=false;
 QueueOnlineEnrichment(owner);
 assert(owner->online_requested==0&&online_enrichment.empty());
 settings.fetch_missing_metadata=true;
 QueueOnlineEnrichment(owner);
 assert(owner->online_requested==3&&online_enrichment.size()==1);
 settings.fetch_missing_metadata=false; // Toggled after queuing, before the renderer grant.
 assert(OnlineEnrichmentScript().empty()&&owner->online_requested==0);
 settings.fetch_missing_lyrics=true;
 QueueOnlineEnrichment(owner);
 assert(owner->online_requested==4&&online_enrichment.size()==1);
 assert(OnlineEnrichmentScript().find(L"metadata:false,album:false,lyrics:true")!=std::wstring::npos);
 settings.fetch_missing_metadata=true;
}

static void CheckNativeArtworkReplies() {
 using namespace history;
 RichMetadata track;track.uri="spotify:track:0000000000000000000001";
 track.artwork_url="https://i.scdn.co/image/"+std::string(40,'a');track.artwork_urls={track.artwork_url};
 ArtworkReply reply;reply.id="7";reply.uri=track.uri;reply.reference=track.artwork_url;
 reply.status="ready";reply.source=2;reply.bytes=artwork_fixture::png();
 std::string reason;
 const auto* owned_bytes=reply.bytes.data();
 assert(!ApplyNativeArtworkReply(track,reply,"8",reason)&&!track.artwork);
 assert(reply.bytes.data()==owned_bytes&&reply.bytes==artwork_fixture::png());
 auto wrong=reply;wrong.uri="spotify:track:0000000000000000000002";
 assert(!ApplyNativeArtworkReply(track,wrong,"7",reason)&&!track.artwork);
 assert(wrong.bytes==artwork_fixture::png());
 wrong=reply;wrong.reference="https://i.scdn.co/image/"+std::string(40,'b');
 assert(!ApplyNativeArtworkReply(track,wrong,"7",reason)&&!track.artwork);
 assert(wrong.bytes==artwork_fixture::png());
 wrong=reply;wrong.bytes={1,2,3};assert(!ApplyNativeArtworkReply(track,wrong,"7",reason)&&!track.artwork);
 wrong=reply;wrong.status="unavailable";wrong.reason="cache-miss";
 assert(!ApplyNativeArtworkReply(track,wrong,"7",reason)&&!track.artwork&&reason=="cache-miss");
 assert(wrong.bytes==artwork_fixture::png());
 assert(ApplyNativeArtworkReply(track,reply,"7",reason));
 assert(track.artwork->bytes.data()==owned_bytes&&reply.bytes.empty());
 assert(reply.source==2&&reply.reference==track.artwork_url);
 assert(track.artwork->bytes==artwork_fixture::png()&&track.artwork->reference==track.artwork_url);
 auto owner=cache.Pin(track);owner->value=track;
 // A delayed native result belongs to the old recording, never the new cover.
 auto current=track;current.artwork.reset();current.artwork_url="https://i.scdn.co/image/"+std::string(40,'b');current.artwork_urls={current.artwork_url};
 owner->value=current;PublishEnrichment(owner,track);assert(!owner->value.artwork);
 PublishArtwork(owner,track);assert(!owner->value.artwork&&"an obsolete in-flight image cannot replace the current cover");
 owner->value.artwork_url=track.artwork_url;owner->value.artwork_urls=track.artwork_urls;
 PublishArtwork(owner,track);assert(owner->value.artwork==track.artwork);
 // A reply arriving after its request ended must not occupy the next slot.
 native_artwork_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(native_artwork_event);
 native_artwork_pending={"9",{},L"test-script",false};
 assert(NativeArtworkScript()==L"test-script"&&NativeArtworkScript().empty());
 auto enqueue=[](std::wstring payload){String value{payload.data(),payload.size(),nullptr};return EnqueueNativeArtwork(&value);};
 assert(enqueue(L"FLOGGFY_ARTWORK_V1:id=8&uri=spotify:track:0000000000000000000001&source=0&status=unavailable&reason=timeout"));
 assert(!native_artwork_pending.received&&native_artwork_pending.reply.empty());
 assert(enqueue(L"FLOGGFY_ARTWORK_V1:id=9&uri=spotify:track:0000000000000000000001&source=0&status=unavailable&reason=cache-miss"));
 assert(native_artwork_pending.received&&WaitForSingleObject(native_artwork_event,0)==WAIT_OBJECT_0);
 const auto accepted=native_artwork_pending.reply;
 assert(enqueue(L"FLOGGFY_ARTWORK_V1:id=9&malformed=second-reply"));assert(native_artwork_pending.reply==accepted);
 native_artwork_pending={};CloseHandle(native_artwork_event);native_artwork_event=nullptr;
}

// No settings or logger threads are started. These tests exercise the real
// enrichment/update gates with an explicitly selected in-memory preference.
static unsigned Preferences(bool metadata,bool updates) {
 history::ExclusiveSrw guard(history::lock);
 history::settings.metadata=metadata;
 history::settings.update_existing_metadata=updates;
 return ++history::settings.generation;
}
static std::vector<uint8_t> ReadBytes(const std::wstring& path) {
 history::settings_file::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
 assert(file.value!=INVALID_HANDLE_VALUE);
 const auto size=GetFileSize(file.value,nullptr);assert(size!=INVALID_FILE_SIZE);
 std::vector<uint8_t> bytes(size);DWORD count=0;
 assert(ReadFile(file.value,bytes.data(),size,&count,nullptr)&&count==size);
 return bytes;
}
static std::vector<uint8_t> AudioBytes(const std::vector<uint8_t>& bytes,bool flac) {
 if(flac) {
  history::FlacInfo info;size_t offset=0;
  assert(history::ParseFlacMetadata(bytes,info,offset)==history::FlacParse::Valid);
  return {bytes.begin()+offset,bytes.end()};
 }
 // Compare complete packets, omitting only the Vorbis comment packet.
 std::vector<uint8_t> audio,packet;unsigned number=0;
 for(size_t page=0;page<bytes.size();) {
  const size_t header=27+bytes[page+26];size_t body=page+header;
  for(size_t at=page+27;at<page+header;++at) {
   const size_t length=bytes[at];
   packet.insert(packet.end(),bytes.begin()+body,bytes.begin()+body+length);body+=length;
   if(length<255) {
    if(number++!=1)audio.insert(audio.end(),packet.begin(),packet.end());
    packet.clear();
   }
  }
  page=body;
 }
 assert(packet.empty());return audio;
}
static void CheckPublication(const std::wstring& root,bool flac) {
 using namespace history;
 Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;
 RichMetadata fixture;fixture.uri=ready_uri;fixture.title="Song";fixture.artist="Artist";fixture.album="Album";fixture.duration=200;
 cache.Put(fixture);
 Catalog catalog;catalog.spotify_uri=ready_uri;
 const auto path=root+(flac?L"\\recording.flac":L"\\recording.ogg");
 Tags basic;
 basic.fields={{"TITLE","Song"},{"ARTIST","Artist"},{"ALBUM","Album"},{"SPOTIFY_URI",ready_uri},
  {"LYRICS","[00:01.000]Saved words"},{"CUSTOM","user metadata"},{"HISTORY_COMPLETE_LISTEN","1"},
  {"HISTORY_CAPTURE",flac?"native compressed FLAC input":"native compressed Ogg pages"}};
 const auto source=flac?recording_fixture::fixture():recording_fixture::ogg();
 std::vector<uint8_t> tagged;std::string error;
 auto save_tags=[&](const Tags& tags) {
  assert(flac?TagFlac(source,tags,tagged,error):TagOgg(source,tags,tagged,error));
 };
 auto inspect=[&]() {
  RecordingFile file;assert(ReadRecording(path,file)&&file.complete&&MatchesRecording(file,catalog));
  assert(std::find(file.tags.fields.begin(),file.tags.fields.end(),std::make_pair(std::string("CUSTOM"),std::string("user metadata")))!=file.tags.fields.end());
  assert(std::find(file.tags.fields.begin(),file.tags.fields.end(),basic.fields[4])!=file.tags.fields.end());
  return file;
 };
 // Metadata=0 leaves basic Windows track fields intact and never requests art.
 Preferences(false,true);const auto reads=artwork_reads;
 auto tags=basic;EnrichTags(media,tags);assert(artwork_reads==reads&&tags.cover.empty());
 save_tags(tags);RecordingFile incoming;
 assert(InspectRecording(tagged.data(),tagged.size(),incoming)&&incoming.complete);
 assert(PublishBytes(path,incoming.quality,tagged.data(),tagged.size(),catalog)==FilePublication::Saved);
 assert(!inspect().has_artwork);
 const auto coverless=ReadBytes(path);assert(AudioBytes(coverless,flac)==AudioBytes(source,flac));
 // With metadata enabled, unavailable/invalid client images also save coverless.
 Preferences(true,true);ready_cover.clear();tags=basic;EnrichTags(media,tags);assert(tags.cover.empty());
 ready_cover={1,2,3};EnrichTags(media,tags);assert(tags.cover.empty());
 save_tags(tags);const auto missing=root+(flac?L"\\unavailable.flac":L"\\unavailable.ogg");
 assert(PublishBytes(missing,incoming.quality,tagged.data(),tagged.size(),catalog)==FilePublication::Saved);
 assert(DeleteFileW(missing.c_str()));
 // A later client cover fills the existing file, respecting opt-out first.
 ready_cover=artwork_fixture::png();ready_mime="image/png";
 auto generation=Preferences(true,false);tags=basic;EnrichTags(media,tags);
 assert(tags.cover==ready_cover&&ArtworkSource(tags)=="spotify-client");
 assert(UpdateRecordingMetadata(path,tags,catalog,generation)==FileMetadataUpdate::Skipped&&ReadBytes(path)==coverless);
 generation=Preferences(true,true);
 assert(UpdateRecordingMetadata(path,tags,catalog,generation)==FileMetadataUpdate::Updated);
 assert(inspect().tags.cover==ready_cover&&AudioBytes(ReadBytes(path),flac)==AudioBytes(coverless,flac));
 // Better client artwork upgrades metadata only; a second identical pass is a no-op.
 ready_cover=artwork_fixture::jpeg();ready_mime="image/jpeg";tags=basic;EnrichTags(media,tags);
 assert(UpdateRecordingMetadata(path,tags,catalog,generation)==FileMetadataUpdate::Updated);
 const auto enriched=ReadBytes(path);
 assert(inspect().tags.cover==ready_cover&&AudioBytes(enriched,flac)==AudioBytes(coverless,flac));
 assert(UpdateRecordingMetadata(path,tags,catalog,generation)==FileMetadataUpdate::Skipped&&ReadBytes(path)==enriched);
 generation=Preferences(false,true);
 assert(UpdateRecordingMetadata(path,basic,catalog,generation)==FileMetadataUpdate::Skipped&&ReadBytes(path)==enriched);
 if(flac) {
  // No new cover and Metadata=0: 16 -> 24-bit audio retains the saved cover,
  // synchronized lyrics and custom fields through the real atomic publisher.
  Tags replacement;replacement.fields={{"SPOTIFY_URI",ready_uri},{"HISTORY_COMPLETE_LISTEN","1"},
   {"HISTORY_CAPTURE","native compressed FLAC input"}};
  assert(TagFlac(recording_fixture::fixture(44100,24),replacement,tagged,error));
  const Quality better{Codec::Flac,44100,2,24,0};
  assert(PublishBytes(path,better,tagged.data(),tagged.size(),catalog)==FilePublication::Upgraded);
  const auto upgraded=inspect();assert(upgraded.quality.bits==24&&upgraded.tags.cover==ready_cover);
  assert(AudioBytes(ReadBytes(path),true)==AudioBytes(tagged,true));
 }
 assert(DeleteFileW(path.c_str()));
}
int main() {
 CheckClientArtwork();CheckOwnedMetadata();CheckNativeArtworkReplies();
 wchar_t temp[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
 const auto root=std::wstring(temp)+L"Floggfy-artwork-policy-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 assert(CreateDirectoryW(root.c_str(),nullptr));
 CheckPublication(root,true);CheckPublication(root,false);
 std::vector<history::RecentDownload> recent;
 assert(history::ReadRecentDownloads(recent)&&recent.empty());
 assert(RemoveDirectoryW(root.c_str()));
 puts("PASS: Spotify artwork selection; coverless FLAC/Ogg publication; metadata-only cover fills/upgrades and opt-out; unchanged compressed audio; existing cover, synced lyrics and custom tags survive 24-bit replacement with Metadata=0");
}
