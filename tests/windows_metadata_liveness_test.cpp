// The real poll/cache worker must stay live while enrichment storage is blocked.
#include "../native/media/metadata_bridge.cpp"
#include <cassert>
#include <atomic>
#include <new>
#include "artwork_fixture.h"
static thread_local bool fail_allocation=false;
void* operator new(std::size_t bytes){if(fail_allocation){fail_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
namespace {
HANDLE blocked=nullptr,release_enrichment=nullptr;
std::atomic<unsigned> polls{0},enrichments{0};
bool exact_uri_test=false,fallback_test=false;
std::atomic<unsigned> cache_reads{0},network_reads{0};
HANDLE second_enrichment_started=nullptr;
int Post(int,history::Task* task){
 ++polls;task->execute(task);task->base.release(&task->base);
 // Simulate the renderer's bounded cached-state reply to each native poll.
 wchar_t payload[]=L"FLOGGFY_PLAYBACK_V1:v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A0000000000000000000001&playback_quality=lossless";
 history::String reply{payload,wcslen(payload),nullptr};assert(history::CurrentPlayback(&reply));return 1;
}
void Send(const wchar_t* year){
 std::wstring value=L"FLOGGFY_METADATA_V1:v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A0000000000000000000001&playback_quality=lossless&DISCNUMBER=2&TRACKNUMBER=7&ALBUMARTIST=Album%20Artist&DATE="+std::wstring(year);
 history::String text{value.data(),value.size(),nullptr};assert(history::Enqueue(&text));
}
}
namespace history {
void EnrichStoredMetadata(RichMetadata& metadata){
 const auto pass=enrichments.fetch_add(1);
 if(pass==0&&!fallback_test){SetEvent(blocked);WaitForSingleObject(release_enrichment,10000);}
 if(exact_uri_test){
  if(pass==1)SetEvent(second_enrichment_started);
  metadata.fields["COMMENT"]=metadata.uri;
  metadata.fields["DATE"]="2024";
  return;
 }
 metadata.fields["COMMENT"]="slow enrichment";
}
bool DownloadAlbumArtwork(const std::string&,std::vector<uint8_t>&){
 assert(fallback_test);++network_reads;return false;
}
bool ReadCachedArtwork(const std::string& url,std::vector<uint8_t>& bytes,std::string& reason){
 if(fallback_test){++cache_reads;if(url.back()=='a'){bytes=artwork_fixture::png();return true;}}
 reason="test-missing";return false;
}
}
namespace {
const std::string uri_a="spotify:track:0000000000000000000001",uri_b="spotify:track:0000000000000000000002";
history::Media ExactMedia(const std::string& uri){
 history::Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;media.spotify_uri=uri;return media;
}
history::RichMetadata Seed(const std::string& uri){
 history::RichMetadata metadata;metadata.title="Song";metadata.artist="Artist";metadata.album="Album";
 metadata.duration=200;metadata.uri=uri;metadata.fields["DATE"]="2025";metadata.fields["COMMENT"]=uri;return metadata;
}
std::string Tag(const std::string& uri,const std::string& name){
 history::Tags tags;history::EnrichTags(ExactMedia(uri),tags);
 for(const auto& field:tags.fields)if(field.first==name)return field.second;
 return {};
}
void WaitTag(const std::string& uri,const std::string& name,const std::string& value){
 const auto deadline=GetTickCount64()+5000;
 while(Tag(uri,name)!=value){assert(GetTickCount64()<deadline);Sleep(5);}
}
void SendSparse(const std::string& uri,const wchar_t* year,const wchar_t* extra=L""){
 auto text=L"FLOGGFY_METADATA_V1:v=1&title=Song&artist=Artist&album=Album&duration=200&uri="+
  std::wstring(uri.begin(),uri.end())+L"&DATE="+year+extra;
 history::String value{text.data(),text.size(),nullptr};assert(history::Enqueue(&value));
}
void WaitIdle(const std::shared_ptr<history::MetadataRecord>& owner){
 const auto deadline=GetTickCount64()+5000;
 for(;;){
  {history::SharedSrw guard(history::cache_lock);if(!owner->queued)return;}
  assert(GetTickCount64()<deadline);Sleep(5);
 }
}
void CreateBarriers(){
 exact_uri_test=true;blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_enrichment=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 second_enrichment_started=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(blocked&&release_enrichment&&second_enrichment_started);
}
void StopEnrichment(){
 history::enrichment_running=false;SetEvent(history::enrichment_event);
 assert(WaitForSingleObject(history::enrichment_thread,5000)==WAIT_OBJECT_0);
}
void LiveEnrichmentFallbacks(){
 using namespace history;
 fallback_test=true;InitSettings(nullptr);
 const auto epoch=CaptureEpoch();
 assert(SetReadSpotifyDiskCache(false)&&SetFetchMissingMetadata(false)&&
        SetFetchMissingArtwork(false)&&SetFetchMissingLyrics(false));
 assert(CaptureEpoch()==epoch&&FlushSettings(5000));
 auto first=Seed(uri_a),second=Seed(uri_b);
 first.fields["LYRICS"]="already available";
 first.artwork_url="https://i.scdn.co/image/"+std::string(40,'a');
 second.artwork_url="https://i.scdn.co/image/"+std::string(40,'b');
 auto first_owner=cache.Pin(first),second_owner=cache.Pin(second);
 message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 metadata_running=true;
 HANDLE worker=CreateThread(nullptr,0,MetadataWorker,nullptr,0,nullptr);assert(worker);
 SendSparse(uri_a,L"2025");WaitTag(uri_a,"LYRICS","already available");
 SendSparse(uri_b,L"2025");WaitIdle(first_owner);WaitIdle(second_owner);
 const auto deadline=GetTickCount64()+5000;
 while(cache_reads.load()!=2){assert(GetTickCount64()<deadline);Sleep(5);}
 WaitIdle(first_owner);WaitIdle(second_owner);
 {
  SharedSrw guard(cache_lock);
  assert(first_owner->value.artwork&&first_owner->value.artwork->bytes==artwork_fixture::png());
  assert(!second_owner->value.artwork&&second_owner->native_artwork_attempted.empty());
  assert(!first_owner->local_attempted&&!second_owner->local_attempted&&online_enrichment.empty());
 }
 for(unsigned i=0;i<10;++i){QueueEnrichment(first_owner);QueueEnrichment(second_owner);}
 assert(cache_reads.load()==2&&network_reads.load()==0&&enrichments.load()==0);
 assert(SetReadSpotifyDiskCache(true)&&SetFetchMissingArtwork(true)&&CaptureEpoch()==epoch);
 const auto resumed=GetTickCount64()+5000;
 while(network_reads.load()!=1||enrichments.load()!=2){assert(GetTickCount64()<resumed);Sleep(5);}
 WaitIdle(first_owner);WaitIdle(second_owner);
 assert(Tag(uri_a,"LYRICS")=="already available"&&cache_reads.load()==2);
 const auto revision=EnrichmentEnableRevision();
 assert(SetFetchMissingMetadata(true)&&SetFetchMissingLyrics(true)&&FlushSettings(5000));
 assert(EnrichmentEnableRevision()==revision+2);
 const auto grants_deadline=GetTickCount64()+5000;
 for(;;){
  {SharedSrw guard(cache_lock);
   if(first_owner->online_requested==3&&second_owner->online_requested==7)break;
  }
  assert(GetTickCount64()<grants_deadline);Sleep(5);
 }
 metadata_running=false;SetEvent(message_event);
 assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
 StopEnrichment();
}
std::string CoverReference(char id){
 return "https://i.scdn.co/image/"+std::string(40,id);
}
void VerifyReviewRegressions(const char* check){
 using namespace history;
 fallback_test=true;
 InitSettings(nullptr);
 assert(SetReadSpotifyDiskCache(false)&&SetFetchMissingMetadata(false)&&
        SetFetchMissingArtwork(false)&&SetFetchMissingLyrics(false));
 const auto a=CoverReference('b'),b=CoverReference('a'),other=CoverReference('c');
 assert(StartEnrichmentWorker());
 if(!strcmp(check,"--alternate-artwork")){
  auto first=Seed(uri_a);
  first.artwork_url=a;first.artwork_urls={a};
  auto owner=cache.Pin(first);
  QueueEnrichment(owner);WaitIdle(owner);
  assert(cache_reads.load()==1&&!owner->value.artwork);
  first.artwork_urls={a,other};
  {ExclusiveSrw guard(cache_lock);cache.Put(first);}
  QueueEnrichment(owner);WaitIdle(owner);
  assert(cache_reads.load()==3&&"changed candidates must retry cached artwork");
  first.artwork_urls={other,a,a};
  {ExclusiveSrw guard(cache_lock);cache.Put(first);}
  QueueEnrichment(owner);WaitIdle(owner);
  assert(cache_reads.load()==3&&"reordered or repeated candidates must not retry");
  first.artwork_urls={a,b};
  {ExclusiveSrw guard(cache_lock);cache.Put(first);}
  QueueEnrichment(owner);WaitIdle(owner);
  assert(owner->value.artwork&&owner->value.artwork->reference==b&&network_reads.load()==0);
  assert(owner->native_artwork_attempted.empty()&&"native fetching remains disabled");
 }
 if(!strcmp(check,"--delayed-artwork")){
  assert(SetFetchMissingMetadata(true)&&SetFetchMissingArtwork(true)&&SetFetchMissingLyrics(true));
  auto first=Seed(uri_b);first.artwork_url=a;first.artwork_urls={a};
  auto owner=cache.Pin(first);
  Frame frame{};
  {ExclusiveSrw guard(frame_lock);polling_frame=&frame;}
  QueueEnrichment(owner);
  const auto deadline=GetTickCount64()+5000;
  while(NativeArtworkScript().empty()){assert(GetTickCount64()<deadline);Sleep(5);}
  // A current client snapshot replaces both the fields and the preferred cover
  // while the actual native-image reply slot is still waiting.
  first.artwork_url=b;first.artwork_urls={b};
  first.album_uri="spotify:album:0000000000000000000007";
  first.fields["DATE"]="2027";first.fields["ISRC"]="fresh-isrc";first.fields["LABEL"]="fresh-label";first.fields["LYRICS"]="fresh words";
  {ExclusiveSrw guard(cache_lock);cache.Put(first);}
  {ExclusiveSrw guard(frame_lock);polling_frame=nullptr;} // No further native replies.
  std::string reply;
  {SharedSrw guard(native_artwork_lock);reply="FLOGGFY_ARTWORK_V1:id="+native_artwork_pending.id;}
  reply+="&uri="+first.uri+"&source=0&status=unavailable&reason=cache-miss";
  std::wstring wide(reply.begin(),reply.end());String value{wide.data(),wide.size(),nullptr};
  assert(EnqueueNativeArtwork(&value));
  StopEnrichment();
  assert(network_reads.load()==0&&"a delayed reply must not authorize obsolete CDN work");
  assert(online_enrichment.empty()&&owner->online_requested==0&&"newer fields already satisfy online needs");
  assert(owner->value.artwork&&owner->value.artwork->reference==b&&Tag(uri_b,"DATE")=="2027");
 }
 if(!strcmp(check,"--grant-refresh")){
  assert(SetFetchMissingMetadata(true)&&SetFetchMissingLyrics(true));
  auto sparse=Seed("spotify:track:0000000000000000000003");
  auto owner=cache.Pin(sparse);
  QueueOnlineEnrichment(owner);
  assert(owner->online_requested==7&&online_enrichment.size()==1);
  auto current=sparse;current.fields["ISRC"]="live";current.fields["LABEL"]="live";
  current.fields["LYRICS"]="live lyrics";current.artwork_url=b;current.album_uri="spotify:album:0000000000000000000008";
  {ExclusiveSrw guard(cache_lock);cache.Put(current);}
  assert(OnlineEnrichmentScript().empty()&&owner->online_requested==0&&"UI dispatch must recheck already satisfied grants");
 }
 if(strcmp(check,"--delayed-artwork"))StopEnrichment();
}
void SparseUriRefreshKeepsOwnFields(){
 CreateBarriers();history::cache.Put(Seed(uri_a));history::cache.Put(Seed(uri_b));
 history::message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);history::metadata_running=true;
 HANDLE worker=CreateThread(nullptr,0,history::MetadataWorker,nullptr,0,nullptr);assert(worker);
 SendSparse(uri_a,L"2026");assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
 WaitTag(uri_a,"DATE","2026");
 assert(Tag(uri_a,"COMMENT")==uri_a&&"sparse exact-URI refresh must preserve its own enrichment");
 SendSparse(uri_b,L"2027");WaitTag(uri_b,"DATE","2027");
 assert(Tag(uri_b,"COMMENT")==uri_b);
 history::Media ambiguous=ExactMedia({});history::Tags tags;history::EnrichTags(ambiguous,tags);
 assert(tags.fields.empty()&&"label-only lookup must remain ambiguous");
 history::metadata_running=false;SetEvent(history::message_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
 SetEvent(release_enrichment);StopEnrichment();CloseHandle(worker);
}
void DelayedUriEnrichmentKeepsNewerLiveFields(){
 CreateBarriers();auto first=Seed(uri_a),second=Seed(uri_b);first.fields.erase("COMMENT");second.fields.erase("COMMENT");
 auto first_owner=history::cache.Pin(first),second_owner=history::cache.Pin(second);
 assert(history::StartEnrichmentWorker());
 history::QueueEnrichment(first_owner);
 assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
 // A newer live refresh arrives while the actual storage boundary is blocked.
 auto live=first;live.fields["DATE"]="2026";live.fields["TRACKNUMBER"]="7";
 {history::ExclusiveSrw guard(history::cache_lock);history::cache.Put(live);}
 history::QueueEnrichment(second_owner);
 // Queue more distinct skipped items than the historical cache can retain.
 // The blocked recording and the pending owned recording must both survive.
 for(unsigned i=3;i<43;++i){
  auto skipped=Seed("spotify:track:"+std::string(20,'0')+(i<10?"0":"")+std::to_string(i));
  std::shared_ptr<history::MetadataRecord> obsolete;
  {history::ExclusiveSrw guard(history::cache_lock);obsolete=history::cache.Pin(skipped);}
  history::QueueEnrichment(obsolete);
 }
 SetEvent(release_enrichment);
 assert(WaitForSingleObject(second_enrichment_started,5000)==WAIT_OBJECT_0);
 WaitTag(uri_b,"COMMENT",uri_b);WaitIdle(first_owner);WaitIdle(second_owner);
 history::QueueEnrichment(first_owner);history::QueueEnrichment(second_owner);
 WaitIdle(first_owner);WaitIdle(second_owner);StopEnrichment();
 assert(enrichments.load()==2&&"one local read per needed owner; obsolete jobs do not consume reads");
 {history::SharedSrw guard(history::cache_lock);
  assert(history::cache.FindUri(uri_a)==&first_owner->value);
  assert(history::cache.FindUri(uri_b)==&second_owner->value);
 }
 assert(Tag(uri_a,"COMMENT")==uri_a&&"delayed enrichment must commit by exact URI despite ambiguous labels");
 assert(Tag(uri_b,"COMMENT")==uri_b&&"enrichment must never cross URI ownership");
 assert(Tag(uri_a,"DATE")=="2026"&&Tag(uri_a,"TRACKNUMBER")=="7");
}
}
int main(int argc,char** argv){
 if(argc>1){
  if(!strcmp(argv[1],"--artwork-revision")){
   auto track=Seed(uri_a);
   track.artwork_url="https://i.scdn.co/image/"+std::string(40,'a');
   auto owner=history::cache.Pin(track);
   auto image=std::make_shared<history::MetadataArtwork>();
   image->reference=track.artwork_url;image->bytes=artwork_fixture::png();image->mime="image/png";
   track.artwork=image;
   const auto before=history::MetadataRevision();
   history::PublishArtwork(owner,track);
   assert(owner->value.artwork==image&&history::MetadataRevision()==before+1);
   history::PublishArtwork(owner,track);
   assert(history::MetadataRevision()==before+1);
  }
  else if(!strcmp(argv[1],"--fallbacks")){
   LiveEnrichmentFallbacks();
   puts("PASS: disabled fallbacks preserve cached artwork and lyrics; re-enable resumes pinned records");
   return 0;
  }
  else if(!strcmp(argv[1],"--alternate-artwork")||!strcmp(argv[1],"--delayed-artwork")||!strcmp(argv[1],"--grant-refresh"))VerifyReviewRegressions(argv[1]);
  else if(!strcmp(argv[1],"--ambiguous-refresh"))SparseUriRefreshKeepsOwnFields();
  else if(!strcmp(argv[1],"--ambiguous-delayed"))DelayedUriEnrichmentKeepsNewerLiveFields();
  else assert(false);
  puts("PASS: exact-URI worker enrichment preserves sparse and newer live metadata");return 0;
 }
 blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_enrichment=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(blocked&&release_enrichment);
 history::message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);history::post_task=Post;
 history::metadata_running=true;history::metadata_polling=true;
 HANDLE worker=CreateThread(nullptr,0,history::MetadataWorker,nullptr,0,nullptr);assert(worker);
 Send(L"2025");assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
 std::shared_ptr<history::MetadataRecord> owner;
 {history::SharedSrw guard(history::cache_lock);owner=history::observed_metadata;assert(owner&&history::online_enrichment.empty());}
 const auto before=polls.load();Sleep(3400);assert(polls.load()>before); // Slow enrichment cannot consume the heartbeat.
 assert(history::ReadPlaybackQuality().identity!=0); // Remains fresh beyond the footer's three-second expiry.
 history::Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;
 assert(history::ReadClientPlaybackQuality(media)=="lossless");
 history::Catalog catalog;history::EnrichCatalog(media,catalog);assert(catalog.disc==2&&catalog.track==7&&catalog.artist==L"Album Artist"&&catalog.spotify_uri=="spotify:track:0000000000000000000001");
 Send(L"2026");
 history::Tags current;
 const auto deadline=GetTickCount64()+5000;
 for(;;){current={};history::EnrichTags(media,current);if(std::find(current.fields.begin(),current.fields.end(),std::make_pair(std::string("DATE"),std::string("2026")))!=current.fields.end())break;assert(GetTickCount64()<deadline);Sleep(5);}
 auto date=std::find_if(current.fields.begin(),current.fields.end(),[](const auto& item){return item.first=="DATE";});assert(date!=current.fields.end()&&date->second=="2026");
 SetEvent(release_enrichment);WaitTag(uri_a,"COMMENT","slow enrichment");WaitIdle(owner);
 assert(Tag(uri_a,"DATE")=="2026");
 // Further snapshots retain enrichment and do not read local storage again.
 Send(L"2027");WaitTag(uri_a,"DATE","2027");WaitIdle(owner);
 assert(enrichments.load()==1&&"local lookup belongs to the recording, not each heartbeat");
 assert(Tag(uri_a,"COMMENT")=="slow enrichment");
 // The actual async producer marker protects newer client values and identity.
 SendSparse(uri_a,L"2024",L"&metadata_source=online&TRACKNUMBER=1&LABEL=late-online-label");
 WaitTag(uri_a,"LABEL","late-online-label");WaitIdle(owner);
 assert(Tag(uri_a,"DATE")=="2027"&&Tag(uri_a,"TRACKNUMBER")=="7");
 assert(Tag(uri_a,"COMMENT")=="slow enrichment");
 assert(history::ReadClientPlaybackQuality(media)=="lossless");
 {history::SharedSrw guard(history::cache_lock);assert(history::observed_metadata==owner);}
 history::metadata_running=false;SetEvent(history::message_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
 history::enrichment_running=false;SetEvent(history::enrichment_event);assert(WaitForSingleObject(history::enrichment_thread,5000)==WAIT_OBJECT_0);
 fail_allocation=true;bool threw=false;try{history::Tags tags;history::EnrichTags(media,tags);}catch(const std::bad_alloc&){threw=true;}
 assert(threw&&TryAcquireSRWLockExclusive(&history::cache_lock));ReleaseSRWLockExclusive(&history::cache_lock);
 puts("PASS: heartbeat and basic metadata survive blocked enrichment, stale enrichment never replaces newer data and allocation failures release cache locks");
}
