// Exercise real hook event handling with downloads off: no complete compressed
// buffers are retained, and original parser return values remain unchanged.
#define UpdateRecordingMetadata CountedMetadataUpdate
#include "../native/audio_history.cpp"
#undef UpdateRecordingMetadata
#include <cassert>
#include <sstream>
#include "artwork_fixture.h"
#include "recording_fixture.h"
static unsigned metadata_update_calls=0;
namespace history {
FileMetadataUpdate UpdateRecordingMetadata(const std::wstring&,const Tags&,const Catalog&,unsigned);
FileMetadataUpdate CountedMetadataUpdate(const std::wstring& path,const Tags& incoming,const Catalog& catalog,unsigned generation) {
 ++metadata_update_calls;return UpdateRecordingMetadata(path,incoming,catalog,generation);
}
std::string ReadClientPlaybackQuality(const Media&) {return {};}
void EnrichCatalog(const Media&,Catalog&) {}
void EnrichTags(const Media &, Tags & tags) {
 if(GetSettings().metadata) {tags.fields.push_back({"GENRE","Rock"});tags.fields.push_back({"DATE","2021-07-23"});tags.fields.push_back({"YEAR","2021"});}
 if(GetSettings().lyrics)tags.fields.push_back({"LYRICS","automatic lyric fill"});
}
} // namespace history
static int parser_calls = 0;
static int32_t Parser(void *, OggPage *) {
  ++parser_calls;
  return 58;
}
static void TestRepeatScheduling() {
  CaptureBuffers buffers;Listen listen;Media current;std::vector<Heard> heard;
  Media media;media.title=L"Repeated";media.artist=L"Artist";media.album=L"Album";media.duration=10;media.playing=true;
  auto sample=[&](double position,double time) {media.position=position;AssociateListen(media,listen,current,heard,buffers,time);};
  auto capture=[&](double born) {auto value=std::make_unique<Capture>();value->lossless=true;value->flac.rate=1;value->coverage.samples=10;value->born=born;return value;};
  sample(0,0);buffers.ready.push_back(capture(0));buffers.ready.push_back(capture(10));
  for(int i=1;i<10;++i)sample(i,i);
  sample(.1,10.1);sample(1.2,11.2);
  assert(heard.size()==1 && heard[0].generation==1 && heard[0].start==0 && heard[0].finish==10 && listen.generation==2);
  assert(CaptureMatchesListen(*buffers.ready[0],heard[0]) && !CaptureMatchesListen(*buffers.ready[1],heard[0]));
  save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(save_event);
  SchedulePublications(heard,buffers,7,11.2);
  SaveJob first;assert(saves.Pop(first) && first.epoch==7 && first.heard.generation==1 && first.capture->listen_generation==1 && first.capture->born==0);
  for(int i=12;i<20;++i)sample(i-10,i);
  sample(.1,20.1);sample(1.2,21.2);
  assert(heard.size()==1 && heard[0].generation==2 && heard[0].start==10 && heard[0].finish==20 && listen.generation==3);
  assert(!CaptureMatchesListen(*first.capture,heard[0])); // A prior instance cannot be reused.
  SchedulePublications(heard,buffers,7,21.2);
  SaveJob second;assert(saves.Pop(second) && second.heard.generation==2 && second.capture->born==10 && buffers.ready.empty());
  CloseHandle(save_event);save_event=nullptr;
  Slot event_slot{};event_slot.kind=AudioEventKind::FlacBegin;event_slot.capture=true;event_slot.epoch=3;event_slot.context=42;
  PlaybackQualityTracker quality;
  ProcessCaptureEvent(event_slot,buffers,quality,false,true,4);assert(buffers.active.empty());
  ProcessCaptureEvent(event_slot,buffers,quality,false,true,3);assert(buffers.active.size()==1);
  event_slot.kind=AudioEventKind::FlacError;ProcessCaptureEvent(event_slot,buffers,quality,false,true,3);assert(buffers.active.empty());
  std::puts("PASS: named codec events honor capture epochs; same-title repeat jobs retain distinct completed generations and old boundaries");
}
enum class LateCapture { Complete,Timeout,Invalid,PastDeadline };
static void TestLateCapture(bool flac,bool repeat,LateCapture outcome) {
  CaptureBuffers buffers;Listen listen;Media current;std::vector<Heard> heard;
  Media media;media.title=L"Late ending";media.artist=L"Artist";media.album=L"Album";media.duration=10;media.playing=true;
  auto sample=[&](double position,double time){media.position=position;AssociateListen(media,listen,current,heard,buffers,time);};
  sample(0,100);
  Slot slot{};slot.context=1;slot.time=100;
  auto page=[&](unsigned sequence,unsigned flags,uint64_t samples) {
    const auto bytes=recording_fixture::page(sequence?std::vector<uint8_t>{0}:recording_fixture::identification(),sequence,flags,samples);
    slot.length=unsigned(bytes.size());memcpy(slot.bytes,bytes.data(),bytes.size());ProcessOggCapture(slot,buffers);
  };
  if(flac) {
    auto c=std::make_unique<Capture>();c->lossless=true;c->metadata=true;c->born=100;
    c->flac={44100,2,16,44100,44100,441000};c->coverage.samples=396900;c->coverage.frames=9;
    buffers.active[1]=std::move(c);
  } else {page(0,2,0);slot.time=109;page(1,0,396900);}
  for(unsigned i=1;i<=19;++i)sample(i*.5,100+i*.5);
  if(!repeat)media.title=L"Next song";
  sample(.1,110.1);
  if(repeat)sample(1.2,111.2);
  assert(heard.size()==1&&heard[0].media.title==L"Late ending");
  assert(buffers.active.count(1)&&buffers.active[1]->retained_until==120);
  // The next decoder is unrelated to retention and must survive its cleanup.
  auto next=std::make_unique<Capture>();next->born=110;buffers.active[2]=std::move(next);
  SchedulePublications(heard,buffers,7,111.2);
  SaveJob job;assert(!saves.Pop(job)&&buffers.active.count(1));
  if(outcome!=LateCapture::Timeout) {
    slot.time=outcome==LateCapture::PastDeadline?120.1:111.3;
    if(flac) {
      slot.kind=AudioEventKind::FlacFrame;
      const uint32_t frame[]={44100,44100,2,0,16,0};
      const uint64_t number=outcome==LateCapture::Invalid?8:9;
      memcpy(slot.bytes,frame,sizeof(frame));memcpy(slot.bytes+24,&number,8);
      ProcessFlacCapture(slot,buffers);
    } else page(outcome==LateCapture::Invalid?3:2,4,441000);
  }
  const auto diagnostic=AssociationFailure(heard[0],buffers,0);
  assert(diagnostic.find("no matching complete capture")!=std::string::npos);
  assert(diagnostic.find("Artist - Late ending")!=std::string::npos);
  SchedulePublications(heard,buffers,7,outcome==LateCapture::Complete?111.3:121);
  if(outcome==LateCapture::Complete) {
    assert(saves.Pop(job)&&job.heard.generation==1&&job.capture->Duration()==10);
    publishing_reserved.fetch_sub(job.reserved);
  } else {
    assert(!saves.Pop(job)&&heard.size()==1);
    SchedulePublications(heard,buffers,7,711); // Recent completed records expire after ten minutes.
  }
  assert(heard.empty()&&!buffers.active.count(1)&&buffers.active.count(2));
}
static void TestAssociationRejection() {
  CaptureBuffers buffers;Heard heard;heard.media.title=L"Expected";heard.media.duration=10;
  heard.start=100;heard.finish=110;heard.generation=1;
  auto complete=[](double duration) {
    auto c=std::make_unique<Capture>();c->lossless=true;c->born=100;c->finished=110;
    c->flac.rate=1;c->coverage.samples=uint64_t(duration);return c;
  };
  buffers.ready.push_back(complete(12));
  assert(!CaptureMatchesListen(*buffers.ready[0],heard));
  assert(AssociationFailure(heard,buffers,0).find("timing_ready=1 duration_ready=0")!=std::string::npos);
  std::vector<Heard> pending{heard};SchedulePublications(pending,buffers,7,121);
  SaveJob job;assert(pending.size()==1&&!saves.Pop(job));
  SchedulePublications(pending,buffers,7,711);assert(pending.empty()&&!saves.Pop(job));
  buffers.ready.clear();buffers.ready.push_back(complete(10));buffers.ready.push_back(complete(10));
  assert(AssociationFailure(heard,buffers,2).find("ambiguous completed captures")!=std::string::npos);
  pending={heard};SchedulePublications(pending,buffers,7,111);
  assert(pending.empty()&&!saves.Pop(job));
  // A later short/abandoned track must not erase the pending prior ready file.
  buffers={};buffers.ready.push_back(complete(10));pending={heard};Listen listen;Media current,media;
  media.title=L"Short next";media.duration=5;media.playing=true;
  AssociateListen(media,listen,current,pending,buffers,108);
  media.title=L"Skipped onward";AssociateListen(media,listen,current,pending,buffers,113);
  assert(buffers.ready.size()==1&&pending.size()==1);
  SchedulePublications(pending,buffers,7,113);
  assert(saves.Pop(job)&&job.heard.media.title==L"Expected");publishing_reserved.fetch_sub(job.reserved);
  // Without a completed listen, an ordinary skip still drops the old capture.
  buffers={};pending.clear();listen={};current={};media.title=L"Skipped";media.duration=10;
  AssociateListen(media,listen,current,pending,buffers,100);
  auto partial=complete(9);buffers.active[1]=std::move(partial);
  media.title=L"Next";AssociateListen(media,listen,current,pending,buffers,105);
  assert(pending.empty()&&buffers.active.empty());
}
static void TestLateAssociation() {
  TestRepeatScheduling();
  save_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(save_event);
  for(bool flac:{false,true}) {
    TestLateCapture(flac,false,LateCapture::Complete);
    TestLateCapture(flac,true,LateCapture::Complete);
    TestLateCapture(flac,false,LateCapture::Timeout);
    TestLateCapture(flac,false,LateCapture::Invalid);
    TestLateCapture(flac,false,LateCapture::PastDeadline);
  }
  TestAssociationRejection();
  CloseHandle(save_event);save_event=nullptr;
  puts("PASS: late Ogg/FLAC completion survives natural transitions and repeats; original deadlines, invalid/mismatched/ambiguous capture rejection and skip cleanup remain intact");
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
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Skipped&&metadata_update_calls==before_updates);
  RecordingFile untouched;assert(ReadRecording(path,untouched));
  for(const auto& field:untouched.tags.fields)assert(field.first!="GENRE"&&field.first!="LYRICS");
  assert(SetUpdateExistingMetadata(true));
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved&&metadata_update_calls==before_updates+1);
  WIN32_FILE_ATTRIBUTE_DATA before{},after{};assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&before));
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Skipped&&metadata_update_calls==before_updates+1);
  assert(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&after)&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0);
  WIN32_FIND_DATAW staged{};HANDLE stage=FindFirstFileW((path+L".complete-*").c_str(),&staged);
  assert(stage==INVALID_HANDLE_VALUE);
  Quality q;assert(ReadQuality(path,q)&&q.bits==24);
  // Lower-quality Vorbis playback can fill a better existing FLAC too.
  write();capture.lossless=false;assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved);
  write();assert(SetLyrics(false));assert(Publish(capture,heard,CaptureEpoch())==Publication::Saved);
  RecordingFile enriched;assert(ReadRecording(path,enriched)&&enriched.quality.bits==24);
  auto field=[&](const char* key){for(const auto& value:enriched.tags.fields)if(value.first==key)return value.second;return std::string();};
  assert(field("GENRE")=="Rock"&&field("YEAR")=="2021"&&field("DATE")=="2021-07-23"&&field("LYRICS").empty());
  assert(Publish(capture,heard,CaptureEpoch())==Publication::Skipped);
  std::vector<RecentDownload> metadata_recent;assert(ReadRecentDownloads(metadata_recent)&&metadata_recent.empty());
  DeleteFileW(path.c_str());assert(SetDownloads(false));
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
  assert(GetSettings().log && !GetSettings().debug_log);
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
    auto c=std::make_unique<Capture>();c->lossless=true;c->flac={rate,2,bits,16,16,16};c->coverage.samples=16;c->coverage.frames=1;
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
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped same quality already exists on disk", "Log Artist - Log Track");
  capture=make(44100,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped better quality already exists on disk", "Log Artist - Log Track");
  capture=make(48000,16);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped existing file retained; incoming audio is not a verified quality upgrade", "Log Artist - Log Track");
  assert(DeleteFileW(path.c_str()));
  assert(EnsureDirectory(path.substr(0,path.find_last_of(L'\\'))));
  HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);DWORD n;
  assert(f!=INVALID_HANDLE_VALUE&&WriteFile(f,"unknown",7,&n,nullptr));CloseHandle(f);
  const auto alternate=ResolveRecordingPath(path,CatalogFor(heard.media));assert(alternate!=path && !alternate.empty());
  capture=make(44100,24);assert(Publish(*capture,heard,CaptureEpoch())==Publication::Saved);
  Quality alternate_quality;assert(ExistingRecording(alternate,CatalogFor(heard.media),alternate_quality));
  f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char preserved[7]{};
  assert(f!=INVALID_HANDLE_VALUE && ReadFile(f,preserved,7,&n,nullptr) && n==7 && !memcmp(preserved,"unknown",7));CloseHandle(f);
  assert(DeleteFileW(path.c_str()) && DeleteFileW(alternate.c_str()));
  assert(SetDownloads(false));assert(Publish(*capture,heard,CaptureEpoch())==Publication::Skipped);
  WaitLog("skipped capture settings changed or downloads/format disabled", "Log Artist - Log Track");
  assert(SetDownloads(true));capture=make(44100,24);capture->data.Flatten();
  assert(Publish(*capture,heard,CaptureEpoch())==Publication::Failed);
  WaitLog("failed tagging failed:","Log Artist - Log Track");
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
    Quality quality;assert(ReadQuality(named_path,quality)&&quality.codec==Codec::Flac);
    assert(DeleteFileW(named_path.c_str()));
  }
  assert(SetDownloads(false));
  std::puts("PASS: normal publication quality/size, accurate skip reasons and six classifier words in successful save paths/titles");
}
int main(int argc,char** argv) {
  if(argc==2&&std::string(argv[1])=="--association"){TestLateAssociation();return 0;}
  TestLateAssociation();
  wchar_t ini_path[2048];GetModuleFileNameW(nullptr,ini_path,2048);std::wstring ini=ini_path;ini.resize(ini.find_last_of(L'\\')+1);ini+=L"SpotifyHistory.ini";
  assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"1",ini.c_str()));
  assert(WritePrivateProfileStringW(L"To Disk",L"DebugLog",L"0",ini.c_str()));
  InitSettings(nullptr);
  StartSongDestinationWorker(true);
  std::vector<RecentDownload> recent;assert(ReadRecentDownloads(recent)&&recent.empty());
  TestLyricsPublication();
  assert(ReadRecentDownloads(recent)&&recent.empty()); // Lyrics-only updates and skips are not downloads.
  TestPublicationLogging();
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
