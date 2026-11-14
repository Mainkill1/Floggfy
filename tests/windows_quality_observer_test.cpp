// Exercise real hook event handling with downloads off: no complete compressed
// buffers are retained, and original parser return values remain unchanged.
#define FLOGGFY_PUBLICATION_TEST
#define UpdateRecordingMetadata CountedMetadataUpdate
#define PublishBytes ControlledPublishBytes
#define TagFlac ControlledTagFlac
#include "../native/audio_history.cpp"
#undef UpdateRecordingMetadata
#undef PublishBytes
#undef TagFlac
#include <cassert>
#include <sstream>
#include "artwork_fixture.h"
#include "recording_fixture.h"
static unsigned metadata_update_calls=0;
static bool fail_publication_once=false;
static bool race_unknown_once=false;
static bool fail_tag_once=false;
static bool change_benign_setting_during_enrich=false;
static std::string extra_lyrics;
static unsigned extra_disc=0;
static std::wstring change_root_during_enrich;
static std::wstring unmatched_change_root;
static std::wstring unmatched_change_phase;
static bool unmatched_throw_once=false;
static bool change_lyrics_during_enrich=false;
static bool change_lyrics_aba_during_enrich=false;
static bool disable_during_enrich=false;
static std::vector<uint8_t> extra_cover;
static bool enrichment_ready=true;
static bool advance_revision_during_enrich=false;
static bool force_metadata_failure=false;
static std::uint64_t enrichment_revision=1;
namespace history {
void PublicationTestPhase(const wchar_t* phase,const std::wstring&) {
 if(unmatched_change_phase==phase){unmatched_change_phase.clear();assert(SetSaveLocation(unmatched_change_root));if(unmatched_throw_once){unmatched_throw_once=false;throw 1;}}
}
bool TagFlac(const std::vector<uint8_t>&,const Tags&,std::vector<uint8_t>&,std::string&);
bool ControlledTagFlac(const std::vector<uint8_t>& source,const Tags& tags,std::vector<uint8_t>& output,std::string& error) {
 if(fail_tag_once){fail_tag_once=false;error="injected failed rebuild";return false;}
 return TagFlac(source,tags,output,error);
}
FilePublication PublishBytes(const std::wstring&,const Quality&,const void*,size_t,const Catalog&,size_t*,PublicationPermit,void*);
FilePublication ControlledPublishBytes(const std::wstring& path,const Quality& quality,const void* bytes,size_t size,const Catalog& catalog,size_t* published,PublicationPermit permit,void* context) {
 if(fail_publication_once){fail_publication_once=false;return FilePublication::Failed;}
 if(race_unknown_once){race_unknown_once=false;HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);DWORD written=0;assert(file!=INVALID_HANDLE_VALUE&&WriteFile(file,"foreign",7,&written,nullptr)&&written==7);CloseHandle(file);}
 return PublishBytes(path,quality,bytes,size,catalog,published,permit,context);
}
std::uint64_t MetadataRevision() {return enrichment_revision;}
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring&,const Tags&,const Catalog&,unsigned);
FileMetadataUpdate CountedMetadataUpdate(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned generation) {
 ++metadata_update_calls;if(force_metadata_failure)return FileMetadataUpdate::Failed;
 return UpdateRecordingMetadata(path,incoming,catalog,generation);
}
std::string ReadClientPlaybackQuality(const Media&) {return {};}
void EnrichCatalog(const Media&,Catalog& catalog) {if(extra_disc)catalog.disc=extra_disc;}
void EnrichTags(const Media &, Tags & tags) {
 if(!change_root_during_enrich.empty()){auto root=change_root_during_enrich;change_root_during_enrich.clear();assert(SetSaveLocation(root));}
 if(change_lyrics_aba_during_enrich){change_lyrics_aba_during_enrich=false;const auto original=GetSettings().lyrics;assert(SetLyrics(!original)&&SetLyrics(original));}
 if(change_lyrics_during_enrich){change_lyrics_during_enrich=false;assert(SetLyrics(!GetSettings().lyrics));}
 if(disable_during_enrich){disable_during_enrich=false;assert(SetDownloads(false));}
 if(change_benign_setting_during_enrich){change_benign_setting_during_enrich=false;assert(SetAutoSkipDownloaded(!GetSettings().auto_skip_downloaded));if(GetSettings().never_pause>=0)assert(SetNeverPause(GetSettings().never_pause==0));assert(SetCaptureSpeed(1));}
 if(!extra_cover.empty()){tags.cover=extra_cover;tags.mime="image/png";}
 if(!extra_lyrics.empty())tags.fields.push_back({"LYRICS",extra_lyrics});
 if(advance_revision_during_enrich){advance_revision_during_enrich=false;++enrichment_revision;return;}
 if(!enrichment_ready)return;
 if(GetSettings().metadata) {tags.fields.push_back({"GENRE","Rock"});tags.fields.push_back({"DATE","2021-07-23"});tags.fields.push_back({"YEAR","2021"});}
 if(GetSettings().lyrics)tags.fields.push_back({"LYRICS","automatic lyric fill"});
}
} // namespace history
static int parser_calls = 0;
static int32_t Parser(void *, OggPage *) {
  ++parser_calls;
  return 58;
}
static void TestBoundPlaybackQuality() {
  for(bool episode:{false,true})for(auto codec:{Codec::Vorbis,Codec::Flac,Codec::Mp3,Codec::Aac}) {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(5);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    Media media;media.title=L"Bound audio";media.source=MediaSource::Spotify;
    media.spotify_uri=episode?"spotify:episode:0000000000000000000001":"spotify:track:0000000000000000000001";
    media.playback_instance="instance";
    auto c=std::make_unique<Capture>();
    auto decoder=ownership::DecoderCodec::Ogg;
    if(codec==Codec::Mp3) {c->mp3=true;c->mp3_scan.rate=44100;c->mp3_scan.channels=2;c->mp3_scan.frames=2;decoder=ownership::DecoderCodec::Mp3;}
    else if(codec==Codec::Aac) {c->aac=true;c->spac.info.rate=44100;c->spac.info.channels=2;decoder=ownership::DecoderCodec::Aac;}
    else if(codec==Codec::Flac) {c->lossless=true;c->metadata=true;c->flac.rate=44100;c->flac.channels=2;c->flac.bits=24;decoder=ownership::DecoderCodec::Flac;}
    else {c->stream.rate=44100;c->stream.channels=2;c->stream.pages=1;}
    c->capture_token=tracker.BeginCapture(decoder,0x5000,0,100,5,1);
    ownership::DecodedActivity decoded{};decoded.owner=0x5000;decoded.calls=1;decoded.produced_floats=8192;
    decoded.first=decoded.last=100;decoded.epoch=5;decoded.sequence=2;tracker.ObserveDecoded(decoded);
    assert(tracker.BindCaptureExact(c->capture_token,media.playback_instance,media.spotify_uri,7,100.1));
    buffers.active[0x5000]=std::move(c);
    PlaybackQualitySnapshot base;base.identity=PlaybackIdentity(media.PlaybackKey());
    const auto q=BoundPlaybackQuality(buffers,media,7,base);
    const auto expected=codec==Codec::Mp3?PlaybackFormat::Mp3:codec==Codec::Aac?PlaybackFormat::Aac:
      codec==Codec::Flac?PlaybackFormat::Flac:PlaybackFormat::Ogg;
    assert(q.decoder_bound&&q.format==expected&&q.rate==44100&&q.channels==2);
    assert(q.bits==(codec==Codec::Flac?24u:0u));
    assert(q.level==(codec==Codec::Flac?PlaybackLevel::Lossless24:PlaybackLevel::Lossy));
    assert(BoundPlaybackQuality(buffers,media,8,base).format==PlaybackFormat::Unknown);
    auto next=media;next.playback_instance="next";
    assert(BoundPlaybackQuality(buffers,next,7,base).format==PlaybackFormat::Unknown);
    buffers.active.begin()->second->ambiguous=true;
    assert(BoundPlaybackQuality(buffers,media,7,base).format==PlaybackFormat::Unknown);
  }
  // Disabling a format's saving must still allow its bounded header observer.
  CaptureBuffers buffers;PlaybackQualityTracker quality;
  quality.Media("disabled-format",L"Disabled format",0,16.0/44100,100);
  Slot slot{};slot.context=1;slot.time=100;slot.epoch=5;slot.capture=false;
  slot.kind=AudioEventKind::FlacBegin;ProcessCaptureEvent(slot,buffers,quality,true,true,5);
  const auto header=recording_fixture::fixture(44100,16);
  slot.kind=AudioEventKind::FlacBytes;slot.length=42;slot.input_length=42;
  std::memcpy(slot.bytes,header.data(),42);ProcessCaptureEvent(slot,buffers,quality,true,true,5);
  assert(quality.Snapshot().format==PlaybackFormat::Flac&&buffers.active.empty());
  std::puts("PASS: bound Ogg/FLAC/MP3/AAC headers supply identical song/episode details without guessing unbound generations");
}
enum class LateCapture { Complete,Timeout,Invalid,PastDeadline };
static std::unique_ptr<Capture> BoundPodcastCandidate(
    ownership::DecoderOwnershipTracker& tracker,bool flac,uintptr_t owner,double duration,uint64_t sequence) {
  auto capture=std::make_unique<Capture>();capture->born=100;capture->finished=110;
  capture->decoder_owner=owner;
  if(flac) {
    capture->lossless=true;capture->metadata=true;capture->flac.rate=1;
    capture->flac.channels=2;capture->flac.bits=16;capture->flac.total_samples=uint64_t(duration);
    capture->coverage.samples=uint64_t(duration);capture->coverage.frames=1;
  } else {
    capture->stream.complete=true;capture->stream.rate=1;capture->stream.channels=2;
    capture->stream.samples=int64_t(duration);capture->stream.pages=2;
  }
  capture->capture_token=tracker.BeginCapture(flac?ownership::DecoderCodec::Flac:
      ownership::DecoderCodec::Ogg,owner,flac?0:42,100,5,sequence);
  ownership::DecodedActivity decoded{};decoded.owner=owner;decoded.calls=1;
  decoded.produced_floats=8192;decoded.first=100;decoded.last=109.9;decoded.epoch=5;
  decoded.sequence=sequence+1;tracker.ObserveDecoded(decoded);
  tracker.CompleteCapture(capture->capture_token,110,sequence+2);
  return capture;
}
static void TestNativeFormatPodcastAssociation() {
  for(bool flac:{false,true}) {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(5);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    const auto owner=uintptr_t(flac?0x6100:0x6200);
    buffers.ready.push_back(BoundPodcastCandidate(tracker,flac,owner,10,1));
    Media episode;episode.source=MediaSource::Spotify;
    episode.spotify_uri="spotify:episode:0000000000000000000001";
    episode.playback_instance=flac?"flac-episode":"ogg-episode";
    episode.title=L"Native episode";episode.album=L"Show";
    episode.duration=10;episode.playing=true;
    uint64_t playback=0;
    if(flac) {
      std::array<playback_identity::Match,playback_identity::kObservationCapacity> matches{};
      matches[0].decoder=owner;matches[0].metadata_generation=7;
      matches[0].role=playback_identity::SnapshotRole::PlayerPlaybackId;
      playback=BindDirectReadyCapture(buffers,episode,7,100.2,matches,1);
    } else {
      tracker.ObservePlayback(episode.playback_instance,episode.spotify_uri,7,100.1);
      playback=BindReadyCapture(buffers,episode,7,100.2);
    }
    assert(playback);
    assert(buffers.ready[0]->listen_generation==7);
  }
  {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(5);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    buffers.ready.push_back(BoundPodcastCandidate(tracker,false,0x6300,8,1));
    Media episode;episode.source=MediaSource::Spotify;
    episode.spotify_uri="spotify:episode:0000000000000000000002";
    episode.playback_instance="duration-mismatch";episode.duration=10;episode.playing=true;
    tracker.ObservePlayback(episode.playback_instance,episode.spotify_uri,8,100.1);
    assert(!BindReadyCapture(buffers,episode,8,100.2));
  }
  {
    ownership::DecoderOwnershipTracker tracker;tracker.Reset(5);
    CaptureBuffers buffers;buffers.ownership_tracker=&tracker;
    buffers.ready.push_back(BoundPodcastCandidate(tracker,false,0x6400,10,1));
    buffers.ready.push_back(BoundPodcastCandidate(tracker,true,0x6500,10,10));
    Media episode;episode.source=MediaSource::Spotify;
    episode.spotify_uri="spotify:episode:0000000000000000000003";
    episode.playback_instance="ambiguous-native";episode.duration=10;episode.playing=true;
    tracker.ObservePlayback(episode.playback_instance,episode.spotify_uri,9,100.1);
    assert(!BindReadyCapture(buffers,episode,9,100.2));
  }
  std::puts("PASS: Ogg/FLAC episodes bind by playback evidence while duration mismatch and ambiguity reject");
}
struct TerminalTailCase {
  const char* name;double tail_time,schedule_time;
    bool reconcile_before_tail,late_start,publish;
};
static void TestLateAssociation() {
  TestBoundPlaybackQuality();
  TestNativeFormatPodcastAssociation();
  puts("PASS: source-independent codec quality and native episode association");
}
static void TestLyricsPublication() {
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-lyric-publication-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetLyrics(true));
  Heard heard;heard.media.title=L"Track";heard.media.artist=L"Artist";heard.media.album=L"Album";heard.media.track=1;
  auto settings=GetSettings();auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  auto source=recording_fixture::fixture(44100,24);
  heard.media.duration=16.0/44100;
  Tags tags;tags.fields={{"TITLE","Track"},{"ARTIST","Artist"},{"ALBUM","Album"},{"TRACKNUMBER","1"},{"HISTORY_MEDIA_DURATION",std::to_string(heard.media.duration)},{"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};tags.cover=artwork_fixture::png();tags.mime="image/png";
  std::vector<uint8_t> existing;std::string error;assert(TagFlac(source,tags,existing,error));
  auto write=[&]{DeleteFileW(path.c_str());HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);DWORD n;assert(WriteFile(f,existing.data(),DWORD(existing.size()),&n,nullptr));CloseHandle(f);};
  Capture capture;capture.lossless=true;capture.flac={44100,2,16,16,16,16};
  write();assert(SetUpdateExistingMetadata(false));
  const auto before_updates=metadata_update_calls;
  assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent&&metadata_update_calls==before_updates);
  RecordingFile untouched;assert(ReadRecording(path,untouched));
  for(const auto& field:untouched.tags.fields)assert(field.first!="GENRE"&&field.first!="LYRICS");
  assert(SetUpdateExistingMetadata(true));
  assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent&&metadata_update_calls==before_updates+1);
  WIN32_FILE_ATTRIBUTE_DATA before{},after{};assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&before));
  assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent&&metadata_update_calls==before_updates+1);
  assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&after)&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0);
  WIN32_FIND_DATAW staged{};HANDLE stage=FindFirstFileW((path+L".complete-*").c_str(),&staged);
  assert(stage==INVALID_HANDLE_VALUE);
  RecordingFile saved;assert(ReadRecording(path,saved)&&saved.complete&&saved.quality.bits==24);
  // Lower-quality Vorbis playback can fill a better existing FLAC too.
  write();capture.lossless=false;assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  write();assert(SetLyrics(false));assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  RecordingFile enriched;assert(ReadRecording(path,enriched)&&enriched.quality.bits==24);
  auto field=[&](const char* key){for(const auto& value:enriched.tags.fields)if(value.first==key)return value.second;return std::string();};
  assert(field("GENRE")=="Rock"&&field("YEAR")=="2021"&&field("DATE")=="2021-07-23"&&field("LYRICS").empty());
  assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  std::vector<RecentDownload> metadata_recent;assert(ReadRecentDownloads(metadata_recent)&&metadata_recent.empty());
  write();force_metadata_failure=true;
  assert(Publish(capture,heard,CaptureEpoch())==Publication::AlreadyPresent&&"optional enrichment failure acknowledges verified existing audio");
  force_metadata_failure=false;RecordingFile retained;assert(ReadRecording(path,retained)&&retained.complete);
  DeleteFileW(path.c_str());assert(SetDownloads(false));
}
static void TestExistingMetadataAtPlaybackStart() {
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-existing-metadata-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetLyrics(true)&&SetUpdateExistingMetadata(true));
  Media media;media.title=L"Existing Track";media.artist=L"Existing Artist";media.album=L"Existing Album";
  media.track=1;media.duration=16.0/44100;media.playing=true;media.spotify_uri="spotify:track:AAAAAAAAAAAAAAAAAAAAAA";
  const auto settings=GetSettings();const auto catalog=CatalogFor(media);
  const auto path=OutputPath(settings.root,catalog,L".flac",settings.music_folder);
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  Tags tags;tags.fields={{"TITLE","Existing Track"},{"ARTIST","Existing Artist"},{"ALBUM","Existing Album"},
    {"TRACKNUMBER","1"},{"SPOTIFY_URI",media.spotify_uri},{"HISTORY_MEDIA_DURATION",std::to_string(media.duration)},
    {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed FLAC input"}};
  std::vector<uint8_t> existing;std::string error;
  assert(TagFlac(recording_fixture::fixture(44100,24),tags,existing,error));
  DeleteFileW(path.c_str());HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
  DWORD written=0;assert(file!=INVALID_HANDLE_VALUE&&WriteFile(file,existing.data(),DWORD(existing.size()),&written,nullptr)&&written==existing.size());CloseHandle(file);
  const auto before=metadata_update_calls;
  enrichment_ready=false;
  PublishCurrentDestination(media,settings);auto initial=TakeFilenameCheck();assert(initial);ProcessFilenameCheck(*initial);
  assert(metadata_update_calls==before+1);
  advance_revision_during_enrich=true;
  PublishCurrentDestination(media,settings);
  if(auto raced=TakeFilenameCheck())ProcessFilenameCheck(*raced);
  enrichment_ready=true;
  PublishCurrentDestination(media,settings);auto enriched=TakeFilenameCheck();assert(enriched);ProcessFilenameCheck(*enriched);
  assert(metadata_update_calls==before+2);
  RecordingFile updated;assert(ReadRecording(path,updated));
  bool lyrics=false;for(const auto& field:updated.tags.fields)if(field.first=="LYRICS"&&field.second=="automatic lyric fill")lyrics=true;
  assert(lyrics);
  PublishCurrentDestination(media,settings);assert(!TakeFilenameCheck());
  DeleteFileW(path.c_str());
  // A final explicit UID conflict redirects the existing-file check to the
  // stable duplicate path, where metadata is still filled during playback.
  auto other_tags=tags;
  for(auto& field:other_tags.fields)if(field.first=="SPOTIFY_URI")field.second="spotify:track:BBBBBBBBBBBBBBBBBBBBBB";
  std::vector<uint8_t> other;assert(TagFlac(recording_fixture::fixture(44100,24),other_tags,other,error));
  auto write_file=[](const std::wstring& target,const std::vector<uint8_t>& bytes) {
    assert(EnsureDirectory(target.substr(0,target.find_last_of(L'\\'))));
    HANDLE output=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);DWORD count=0;
    assert(output!=INVALID_HANDLE_VALUE&&WriteFile(output,bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size());CloseHandle(output);
  };
  write_file(path,other);
  const auto duplicate=DuplicateOutputPath(settings.root,catalog,L".flac",settings.music_folder);
  write_file(duplicate,existing);
  ++enrichment_revision;const auto duplicate_before=metadata_update_calls;
  PublishCurrentDestination(media,GetSettings());auto duplicate_check=TakeFilenameCheck();assert(duplicate_check);ProcessFilenameCheck(*duplicate_check);
  assert(metadata_update_calls==duplicate_before+1);
  RecordingFile duplicate_updated;assert(ReadRecording(duplicate,duplicate_updated)&&MatchesRecording(duplicate_updated,catalog));
  lyrics=false;for(const auto& field:duplicate_updated.tags.fields)if(field.first=="LYRICS"&&field.second=="automatic lyric fill")lyrics=true;
  assert(lyrics);
  assert(DeleteFileW(path.c_str())&&DeleteFileW(duplicate.c_str()));
  write_file(duplicate,existing);++enrichment_revision;
  const auto orphaned_duplicate_before=metadata_update_calls;
  PublishCurrentDestination(media,GetSettings());auto orphaned_check=TakeFilenameCheck();assert(orphaned_check);
  ProcessFilenameCheck(*orphaned_check);
  assert(metadata_update_calls==orphaned_duplicate_before+1&&
         ReadRecording(duplicate,duplicate_updated)&&MatchesRecording(duplicate_updated,catalog)&&
         "metadata lookup must retain an established duplicate after its canonical conflict is deleted");
  assert(DeleteFileW(duplicate.c_str()));
  std::puts("PASS: existing owned recording receives delayed lyrics during partial playback");
}
static void TestAutoSkipDownloaded() {
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-auto-skip-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetAutoSkipDownloaded(true)&&SetUpdateExistingMetadata(false));
  Media media;media.source=MediaSource::Spotify;media.title=L"Downloaded Track";media.artist=L"Downloaded Artist";
  media.album=L"Downloaded Album";media.track=1;media.duration=1;media.playing=true;
  media.spotify_uri="spotify:track:0000000000000000000001";media.playback_instance="playback-1";
  const auto settings=GetSettings();const auto catalog=CatalogFor(media);
  const auto path=OutputPath(settings.root,catalog,L".ogg",settings.music_folder);
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  Tags tags;tags.fields={{"TITLE","Downloaded Track"},{"ARTIST","Downloaded Artist"},{"ALBUM","Downloaded Album"},
    {"TRACKNUMBER","1"},{"SPOTIFY_URI",media.spotify_uri},{"HISTORY_MEDIA_DURATION","1"},
    {"HISTORY_COMPLETE_LISTEN","1"},{"HISTORY_CAPTURE","native compressed Ogg pages"}};
  std::vector<uint8_t> owned;std::string error;assert(TagOgg(recording_fixture::ogg(),tags,owned,error));
  DeleteFileW(path.c_str());HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
  DWORD written=0;assert(file!=INVALID_HANDLE_VALUE&&WriteFile(file,owned.data(),DWORD(owned.size()),&written,nullptr)&&written==owned.size());CloseHandle(file);
  auto publish_quality=[&](PlaybackFormat format,unsigned rate,unsigned channels,unsigned bits=0) {
    PlaybackQualitySnapshot quality;quality.identity=PlaybackIdentity(media.PlaybackKey());quality.format=format;
    quality.rate=rate;quality.channels=channels;quality.bits=bits;PublishPlaybackQuality(quality);
  };
  const auto initial_token=ReadAutoSkipRequest().token;
  publish_quality(PlaybackFormat::Ogg,44100,2);
  PublishCurrentDestination(media,GetSettings());auto check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
  const auto first=ReadAutoSkipRequest();
  assert(first.token>initial_token&&first.uri==media.spotify_uri&&first.settings_generation==GetSettings().generation);
  PublishCurrentDestination(media,GetSettings());assert(!TakeFilenameCheck());
  ProcessFilenameCheck(*check);assert(ReadAutoSkipRequest().token==first.token);

  media.playback_instance="playback-2";
  publish_quality(PlaybackFormat::Unknown,0,0);
  PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
  assert(ReadAutoSkipRequest().token==first.token);
  publish_quality(PlaybackFormat::Ogg,44100,2);
  PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
  const auto second=ReadAutoSkipRequest();assert(second.token>first.token);

  media.playback_instance="playback-3";
  publish_quality(PlaybackFormat::Flac,44100,2,16);
  PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
  assert(ReadAutoSkipRequest().token==second.token);

  assert(SetUpdateExistingMetadata(true));media.playback_instance="playback-4";
  publish_quality(PlaybackFormat::Ogg,44100,2);force_metadata_failure=true;
  PublishCurrentDestination(media,GetSettings());check=TakeFilenameCheck();assert(check);ProcessFilenameCheck(*check);
  force_metadata_failure=false;assert(ReadAutoSkipRequest().token==second.token);

  assert(SetUpdateExistingMetadata(false));
  Media episode=media;episode.spotify_uri="spotify:episode:0000000000000000000001";episode.playback_instance="episode-1";
  PublishCurrentDestination(episode,GetSettings());assert(!TakeFilenameCheck());
  assert(ReadAutoSkipRequest().token==second.token);
  assert(DeleteFileW(path.c_str()));assert(SetAutoSkipDownloaded(false));
  std::puts("PASS: auto-skip waits for exact quality and successful metadata, preserves upgrades, deduplicates instances and ignores podcasts");
}
static std::string ReadActivityLog() {
  const auto path=WindowsPath(GetSettings().root+L"\\Floggfy.log");
  HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
  if(f==INVALID_HANDLE_VALUE)return {};
  DWORD size=GetFileSize(f,nullptr),n=0;std::string text;
  if(size!=INVALID_FILE_SIZE&&size<=5*1024*1024){text.resize(size);if(!ReadFile(f,text.data(),size,&n,nullptr))text.clear();else text.resize(n);}
  CloseHandle(f);return text;
}
static void WaitLog(const std::string& prefix,const std::string& suffix) {
  const auto deadline=GetTickCount64()+5000;
  while(GetTickCount64()<deadline){
    std::istringstream lines(ReadActivityLog());std::string line;
    while(std::getline(lines,line))if(line.find(prefix)!=std::string::npos&&line.find(suffix)!=std::string::npos)return;
    Sleep(10);
  }
  std::fprintf(stderr,"Missing activity log: %s ... %s\n",prefix.c_str(),suffix.c_str());assert(false);
}
static void TestPublicationLogging() {
  assert(GetSettings().log_level==2);
  assert(SavedAudioDetails({Codec::Vorbis,44100,2,0,320000},1048576)=="Ogg Vorbis 320 kbps nominal, 44,100 Hz | 1.00 MiB (1048576 bytes)");
  assert(SavedAudioDetails({Codec::Vorbis,44100,2,0,0},1048576)=="Ogg Vorbis, 44,100 Hz | 1.00 MiB (1048576 bytes)");
  wchar_t temp[2048];GetTempPathW(2048,temp);
  assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-publication-log-"+std::to_wstring(GetCurrentProcessId())));
  assert(SetDownloads(true)&&SetFlac(true)&&SetOgg(true)&&SetLyrics(false));
  Heard heard;heard.media.title=L"Log Track";heard.media.artist=L"Log Artist";heard.media.album=L"Log Album";heard.media.track=1;
  heard.media.duration=16.0/44100;
  const auto settings=GetSettings();const auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
  // A planned path stays disabled until actual saved audio exists.
  PublishCurrentDestination(heard.media,settings);
  std::wstring song_file;
  assert(!ReadSongDestination(PlaybackIdentity(heard.media.Key()),settings.generation,song_file));
  auto make=[](unsigned rate,unsigned bits){
    auto c=std::make_unique<Capture>();c->lossless=true;c->metadata=true;c->flac={rate,2,bits,16,16,16};c->coverage.samples=16;c->coverage.frames=1;
    auto bytes=recording_fixture::fixture(rate,bits);
    c->bytes=bytes.size();assert(c->data.Append(bytes.data(),bytes.size(),65536));return c;
  };
  auto capture=make(44100,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  PublishCurrentDestination(heard.media,GetSettings());
  const auto song_deadline=GetTickCount64()+5000;
  while(!ReadSongDestination(PlaybackIdentity(heard.media.Key()),GetSettings().generation,song_file) && GetTickCount64()<song_deadline)Sleep(1);
  assert(song_file==path);
  WIN32_FILE_ATTRIBUTE_DATA attributes{};assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attributes));
  auto size=std::to_string((uint64_t(attributes.nFileSizeHigh)<<32)|attributes.nFileSizeLow);
  WaitLog("finished FLAC 16-bit, 44,100 Hz", "("+size+" bytes) | Log Artist - Log Track");
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attributes));size=std::to_string((uint64_t(attributes.nFileSizeHigh)<<32)|attributes.nFileSizeLow);
  WaitLog("upgraded FLAC 24-bit, 44,100 Hz", "("+size+" bytes) | Log Artist - Log Track");
  std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.size()==1);
  assert(recent[0].path==path && recent[0].title==L"Log Artist - Log Track" && recent[0].bytes==std::stoull(size));
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  WaitLog("skipped same quality already exists on disk", "Log Artist - Log Track");
  capture=make(44100,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  WaitLog("skipped better quality already exists on disk", "Log Artist - Log Track");
  capture=make(48000,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::AlreadyPresent);
  WaitLog("skipped existing file retained; incoming audio is not a verified quality upgrade", "Log Artist - Log Track");
  assert(DeleteFileW(path.c_str()));
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);DWORD n;
  assert(f!=INVALID_HANDLE_VALUE&&WriteFile(f,"unknown",7,&n,nullptr));CloseHandle(f);
  const auto old_alternate=ResolveRecordingDestination(path,CatalogFor(heard.media)).path;
  assert(old_alternate!=path && !old_alternate.empty());
  const auto alternate=DuplicateOutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  Quality alternate_quality;assert(ExistingRecording(alternate,CatalogFor(heard.media),alternate_quality));
  assert(GetFileAttributesW(old_alternate.c_str())==INVALID_FILE_ATTRIBUTES);
  f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char preserved[7]{};
  assert(f!=INVALID_HANDLE_VALUE && ReadFile(f,preserved,7,&n,nullptr) && n==7 && !memcmp(preserved,"unknown",7));CloseHandle(f);
  assert(DeleteFileW(path.c_str()) && DeleteFileW(alternate.c_str()));
  assert(SetDownloads(false));assert(Publish(*capture,heard,CaptureEpoch())==Publication::Cancelled);
  WaitLog("skipped capture settings changed or downloads/format disabled", "Log Artist - Log Track");
  assert(SetDownloads(true));capture=make(44100,24);capture->data.Flatten();
  assert(Publish(*capture,heard,CaptureEpoch())==Publication::Blocked);
  WaitLog("failed final cached capture validation failed; no file published","Log Artist - Log Track");
  assert(ReadRecentDownloads(recent)&&recent.size()==2); // Only the two actual publication paths add entries.
  // Real publication paths and labels may contain every old classifier word.
  // Each successful save must have just its normal finished outcome.
  for(const wchar_t* word:{L"failed",L"discarded",L"rejected",L"overflow",L"unsupported",L"stopped after"}) {
    Heard named=heard;named.media.title=std::wstring(word)+L" recording";
    auto named_capture=make(44100,16);assert(Publish(*named_capture,named,CaptureEpoch())==Publication::Saved);
    const auto identity=Utf8(named.media.artist)+" - "+Utf8(named.media.title);
    WaitLog("finished FLAC 16-bit, 44,100 Hz",identity);
    std::istringstream lines(ReadActivityLog());std::string line;unsigned outcomes=0;
    while(std::getline(lines,line))if(line.find(identity)!=std::string::npos){
      assert(line.find(" finished FLAC ")!=std::string::npos);++outcomes;
    }
    assert(outcomes==1);
    const auto named_path=OutputPath(GetSettings().root,CatalogFor(named.media),L".flac",GetSettings().music_folder);
    RecordingFile named_recording;assert(ReadRecording(named_path,named_recording)&&named_recording.complete&&named_recording.quality.codec==Codec::Flac);
    assert(DeleteFileW(named_path.c_str()));
  }
  assert(SetDownloads(false));
  std::puts("PASS: normal publication quality/size, accurate skip reasons and six classifier words in successful save paths/titles");
}
static std::unique_ptr<Capture> RecoveryCapture() {
 auto capture=std::make_unique<Capture>();capture->lossless=true;capture->metadata=true;
 capture->flac={44100,2,24,16,16,16};capture->coverage.samples=16;capture->coverage.frames=1;
 const auto source=recording_fixture::fixture(44100,24);capture->bytes=source.size();
 assert(capture->data.Append(source.data(),source.size(),65536));return capture;
}
static void TestUnmatchedRootRecovery() {
 wchar_t temp[2048];GetTempPathW(2048,temp);
 for(const wchar_t* phase:{L"unmatched-json",L"unmatched-audio",L"unmatched-committed",L"unmatched-json-return"}) {
  const auto root=std::wstring(temp)+L"Floggfy-unmatched-replan-"+std::to_wstring(GetCurrentProcessId())+phase;
  assert(SetSaveLocation(root+L"-old")&&SetDownloads(true));
  SaveJob job;job.capture=RecoveryCapture();job.epoch=CaptureEpoch();job.unmatched=true;job.correlation="{\"reason\":\"fixture\"}\n";
  const bool return_root=std::wstring(phase)==L"unmatched-json-return";
  unmatched_change_root=root+L"-new";unmatched_change_phase=return_root?L"unmatched-json":phase;
  const bool after_commit=std::wstring(phase)==L"unmatched-committed";
  unmatched_throw_once=after_commit;
  auto first=Publication::RetryIO;bool interrupted=false;
  try {first=PublishUnmatched(job);}catch(int){interrupted=true;}
  assert(interrupted==after_commit);
  assert(job.capture->unmatched_attempts.size()==1);
  const auto old=job.capture->unmatched_attempts[0].base;
  if(std::wstring(phase)==L"unmatched-committed") {
   assert(!job.capture->publication_source.empty()&&"commit-before-ack exception retains source ownership");
   assert(PublishUnmatched(job)==Publication::Saved&&job.capture->unmatched_attempts.size()==1);
   assert(GetFileAttributesW((old+L".commit").c_str())!=INVALID_FILE_ATTRIBUTES);
  } else {
   assert(first==Publication::Replan);
   assert(GetFileAttributesW((old+L".json").c_str())!=INVALID_FILE_ATTRIBUTES&&"old partial pair survives until replacement commits");
   assert(GetFileAttributesW((old+L".commit").c_str())==INVALID_FILE_ATTRIBUTES);
   if(return_root)assert(SetSaveLocation(root+L"-old"));
   if(std::wstring(phase)==L"unmatched-json") {
    HANDLE held=CreateFileW((old+L".json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(held!=INVALID_HANDLE_VALUE);
    assert(PublishUnmatched(job)==Publication::RetryIO&&"inaccessible old cleanup remains pending after replacement commits");
    assert(!job.capture->publication_source.empty());CloseHandle(held);
   }
   assert(PublishUnmatched(job)==Publication::Saved&&job.capture->unmatched_attempts.size()==2);
   const auto current=job.capture->unmatched_attempts[1].base;assert(current!=old);
   assert(GetFileAttributesW((current+L".commit").c_str())!=INVALID_FILE_ATTRIBUTES);
   RecordingFile file;assert(ReadRecording(current+L".flac",file)&&file.complete);
   assert(GetFileAttributesW((old+L".json").c_str())==INVALID_FILE_ATTRIBUTES);
   assert(GetFileAttributesW((old+L".flac").c_str())==INVALID_FILE_ATTRIBUTES);
  }
  for(const auto& attempt:job.capture->unmatched_attempts)
   for(const wchar_t* extension:{L".json",L".flac",L".commit"})DeleteFileW((attempt.base+extension).c_str());
 }
 std::puts("PASS: unmatched root replans retain partial pairs and recover committed pairs before acknowledgement");
}
static void TestRacedExistingAcknowledgement() {
 wchar_t temp[2048];GetTempPathW(2048,temp);
 assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-raced-ack-"+std::to_wstring(GetCurrentProcessId()))&&SetDownloads(true)&&SetFlac(true));
 Heard heard;heard.media.title=L"Raced recording";heard.media.artist=L"Artist";heard.media.album=L"Album";heard.media.duration=16.0/44100;
 auto capture=RecoveryCapture();race_unknown_once=true;
 const auto raced=Publish(*capture,heard,CaptureEpoch());
 if(raced!=Publication::Replan)std::fprintf(stderr,"raced result=%u error=%s\n",unsigned(raced),capture->publication_error.c_str());
 assert(raced==Publication::Replan&&"an unowned raced destination cannot acknowledge saved audio");
 assert(!capture->publication_source.empty());
 assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
 const auto settings=GetSettings();const auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
 Quality quality;assert(!ExistingRecording(path,CatalogFor(heard.media),quality));
 const auto duplicate=DuplicateOutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
 assert(ExistingRecording(duplicate,CatalogFor(heard.media),quality));
 assert(DeleteFileW(path.c_str())&&DeleteFileW(duplicate.c_str()));
 std::puts("PASS: raced foreign destinations retain source and replan before acknowledgement");
}
static void TestPublicationReplan() {
 wchar_t temp[2048];GetTempPathW(2048,temp);
 const auto root=std::wstring(temp)+L"Floggfy-plan-"+std::to_wstring(GetCurrentProcessId());
 assert(SetSaveLocation(root)&&SetDownloads(true)&&SetFlac(true)&&SetLyrics(false));
 Heard heard;heard.media.title=L"Changed plans";heard.media.artist=L"Artist";heard.media.duration=16.0/44100;
 for(unsigned action=0;action<5;++action) {
  auto capture=RecoveryCapture();
  if(action==0)change_root_during_enrich=root+L"-new";
  if(action==1)change_lyrics_during_enrich=true;
  if(action==2)advance_revision_during_enrich=true;
  if(action==3)disable_during_enrich=true;
  if(action==4)change_lyrics_aba_during_enrich=true;
  assert(Publish(*capture,heard,CaptureEpoch())==(action==3?Publication::Cancelled:Publication::Replan));
  assert(capture->data.size()||!capture->publication_source.empty());
  if(action==3)assert(SetDownloads(true));
 }
 std::puts("PASS: stale destination/artifact plans retain audio and explicit disable cancels separately");
}
static void TestPublicationRecovery(bool benign,bool stable=false) {
 wchar_t temp[2048];GetTempPathW(2048,temp);
 assert(SetSaveLocation(std::wstring(temp)+L"Floggfy-recovery-"+std::to_wstring(GetCurrentProcessId())));
 assert(SetDownloads(true)&&SetFlac(true)&&SetLyrics(false));
 Heard heard;heard.media.title=L"Retry Track";heard.media.artist=L"Retry Artist";heard.media.album=L"Retry Album";
 heard.media.track=1;heard.media.duration=16.0/44100;heard.media.spotify_uri="spotify:track:0000000000000000000001";
 Capture capture;capture.lossless=true;capture.metadata=true;capture.flac={44100,2,24,16,16,16};capture.coverage.samples=16;capture.coverage.frames=1;
 const auto source=recording_fixture::fixture(44100,24);capture.bytes=source.size();assert(capture.data.Append(source.data(),source.size(),65536));
 if(benign) {
  change_benign_setting_during_enrich=true;
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved&&"unrelated settings must preserve completed audio");
 } else {
  fail_publication_once=true;assert(Publish(capture,heard,CaptureEpoch())==Publication::RetryIO);
  if(!stable){heard.media.spotify_uri="spotify:track:0000000000000000000002";heard.media.track=7;extra_disc=2;}
  extra_lyrics="changed lyrics after write failure";extra_cover=artwork_fixture::png();++enrichment_revision;
  const auto prior_artifact=capture.publication_artifact.bytes,prior_source=capture.publication_source;
  fail_tag_once=true;
  assert(Publish(capture,heard,CaptureEpoch())==Publication::RetryIO);
  assert(capture.publication_artifact.bytes==prior_artifact&&capture.publication_source==prior_source&&"failed replacement preserves both recovery representations");
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved&&"retry bytes, catalog and enrichment must match");
 }
 const auto settings=GetSettings();const auto path=OutputPath(settings.root,CatalogFor(heard.media),L".flac",settings.music_folder);
 RecordingFile file;assert(ReadRecording(path,file)&&file.complete&&MatchesRecording(file,CatalogFor(heard.media)));
 if(!benign) {
  auto field=[&](const char* key){for(const auto& value:file.tags.fields)if(value.first==key)return value.second;return std::string();};
  if(!stable)assert(field("TRACKNUMBER")=="7"&&field("DISCNUMBER")=="2");
  assert(field("LYRICS")==extra_lyrics&&file.tags.cover==extra_cover);
  assert(!capture.publication_source.empty()&&"retry keeps the independent source until terminal acknowledgement");
 }
 assert(DeleteFileW(path.c_str()));extra_lyrics.clear();extra_cover.clear();extra_disc=0;
 std::puts("PASS: completed publication preserves source and coherent retry artifacts");
}
int main(int argc,char** argv) {
  if(argc==2&&std::string(argv[1])=="--association"){TestLateAssociation();return 0;}
  TestLateAssociation();
  wchar_t ini_path[2048];GetModuleFileNameW(nullptr,ini_path,2048);std::wstring ini=ini_path;ini.resize(ini.find_last_of(L'\\')+1);ini+=L"SpotifyHistory.ini";
  assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"2",ini.c_str()));
  assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"0",ini.c_str()));
  InitSettings(nullptr);
  StartSongDestinationWorker(true);
  if(argc==2&&std::string(argv[1])=="--recovery-race"){TestRacedExistingAcknowledgement();return 0;}
  if(argc==2&&std::string(argv[1])=="--recovery-unmatched"){TestUnmatchedRootRecovery();return 0;}
  if(argc==2&&std::string(argv[1])=="--recovery-plan"){TestPublicationReplan();return 0;}
  if(argc==2&&std::string(argv[1])=="--recovery-benign"){TestPublicationRecovery(true);return 0;}
  if(argc==2&&std::string(argv[1])=="--recovery-retry"){TestPublicationRecovery(false);return 0;}
  if(argc==2&&std::string(argv[1])=="--recovery-stable"){TestPublicationRecovery(false,true);return 0;}
  std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.empty());
  TestExistingMetadataAtPlaybackStart();
  TestAutoSkipDownloaded();
  TestLyricsPublication();
  assert(ReadRecentDownloads(recent)&&recent.empty()); // Lyrics-only updates and skips are not downloads.
  TestPublicationLogging();
  TestPublicationRecovery(true);TestPublicationRecovery(false);TestPublicationRecovery(false,true);
  TestPublicationReplan();TestUnmatchedRootRecovery();TestRacedExistingAcknowledgement();
  assert(SetDownloads(false)); // Explicit fixture preference, independent of install defaults.
  assert(!GetSettings().downloads);
  queue =
      static_cast<Slot *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Slot) * capacity));
  assert(queue);
  event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  assert(event);
  quality_enabled = true;
  std::vector<uint8_t> data(70000);
  FlacEvent(reinterpret_cast<void *>(2), AudioEventKind::FlacBytes, data.data(), data.size());
  Slot first, second;
  assert(Pop(first) && Pop(second) && !Pop(first));
  assert(!first.capture && !second.capture && first.length == 42 && second.length == 42);
  assert(first.input_length + second.input_length == 70000);
  unsigned char header[28] = {}, body[30] = {};
  OggPage page{header, 28, body, 30};
  original = Parser;
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 1);
  Slot observed;
  assert(Pop(observed) && observed.kind == AudioEventKind::OggPage && !observed.capture && observed.length == 58);
  quality_enabled = false;
  FlacEvent(reinterpret_cast<void *>(2), AudioEventKind::FlacBytes, data.data(), data.size());
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 2 && !Pop(observed));
  CloseHandle(event);
  event = nullptr;
  HeapFree(GetProcessHeap(), 0, queue);
  queue = nullptr;
  std::puts("PASS: downloads-off observation, bounded FLAC prefix copy, complete byte accounting "
            "and unchanged original decoder returns");
}
