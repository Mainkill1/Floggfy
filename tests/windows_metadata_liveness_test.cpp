// The real poll/cache worker must stay live while enrichment storage is blocked.
#include "../native/metadata_bridge.cpp"
#include <cassert>
#include <atomic>
#include <new>
static thread_local bool fail_allocation=false;
void* operator new(std::size_t bytes){if(fail_allocation){fail_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
namespace {
HANDLE blocked=nullptr,release_enrichment=nullptr,refresh_blocked=nullptr,release_refresh=nullptr;
std::atomic<unsigned> polls{0},enrichments{0};
bool exact_uri_test=false;
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
 if(pass==0){SetEvent(blocked);WaitForSingleObject(release_enrichment,10000);}
 if(exact_uri_test){
  if(pass==1)SetEvent(second_enrichment_started);
  metadata.fields["COMMENT"]=metadata.uri;
  metadata.fields["DATE"]="2024";
  return;
 }
 if(pass==2){SetEvent(refresh_blocked);WaitForSingleObject(release_refresh,10000);}
 metadata.fields["COMMENT"]="slow enrichment";
}
bool CacheAlbumArtwork(const std::string&,const std::string&){return false;}
bool GetAlbumArtwork(const std::string&,std::vector<uint8_t>&,std::string&){return false;}
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
void SendSparse(const std::string& uri,const wchar_t* year){
 auto text=L"FLOGGFY_METADATA_V1:v=1&title=Song&artist=Artist&album=Album&duration=200&uri="+
  std::wstring(uri.begin(),uri.end())+L"&DATE="+year;
 history::String value{text.data(),text.size(),nullptr};assert(history::Enqueue(&value));
}
void CreateBarriers(){
 exact_uri_test=true;blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_enrichment=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 second_enrichment_started=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(blocked&&release_enrichment&&second_enrichment_started);
}
void StopEnrichment(){
 history::enrichment_running=false;SetEvent(history::enrichment_event);
 assert(WaitForSingleObject(history::enrichment_thread,5000)==WAIT_OBJECT_0);
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
 history::cache.Put(first);history::cache.Put(second);
 assert(history::StartEnrichmentWorker());
 history::QueueEnrichment(first,history::GetSettings().generation);
 assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
 // A newer live refresh arrives while the actual storage boundary is blocked.
 auto live=first;live.fields["DATE"]="2026";live.fields["TRACKNUMBER"]="7";
 {history::ExclusiveSrw guard(history::cache_lock);history::cache.Put(live);}
 history::QueueEnrichment(second,history::GetSettings().generation);SetEvent(release_enrichment);
 assert(WaitForSingleObject(second_enrichment_started,5000)==WAIT_OBJECT_0);StopEnrichment();
 assert(Tag(uri_a,"COMMENT")==uri_a&&"delayed enrichment must commit by exact URI despite ambiguous labels");
 assert(Tag(uri_b,"COMMENT")==uri_b&&"enrichment must never cross URI ownership");
 assert(Tag(uri_a,"DATE")=="2026"&&Tag(uri_a,"TRACKNUMBER")=="7");
}
}
int main(int argc,char** argv){
 if(argc>1){
  if(!strcmp(argv[1],"--ambiguous-refresh"))SparseUriRefreshKeepsOwnFields();
  else if(!strcmp(argv[1],"--ambiguous-delayed"))DelayedUriEnrichmentKeepsNewerLiveFields();
  else assert(false);
  puts("PASS: exact-URI worker enrichment preserves sparse and newer live metadata");return 0;
 }
 refresh_blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_refresh=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_enrichment=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(blocked&&release_enrichment);
 history::message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);history::post_task=Post;
 history::metadata_running=true;history::metadata_polling=true;
 HANDLE worker=CreateThread(nullptr,0,history::MetadataWorker,nullptr,0,nullptr);assert(worker);
 Send(L"2025");assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
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
 SetEvent(release_enrichment);while(enrichments.load()<2){assert(GetTickCount64()<deadline);Sleep(5);}
 current={};history::EnrichTags(media,current);date=std::find_if(current.fields.begin(),current.fields.end(),[](const auto& item){return item.first=="DATE";});assert(date!=current.fields.end()&&date->second=="2026");
 // An unchanged client heartbeat must retain already obtained richer fields,
 // even while its next enrichment is blocked again.
 Send(L"2026");assert(WaitForSingleObject(refresh_blocked,5000)==WAIT_OBJECT_0);
 current={};history::EnrichTags(media,current);
 assert(std::find(current.fields.begin(),current.fields.end(),std::make_pair(std::string("COMMENT"),std::string("slow enrichment")))!=current.fields.end());
 SetEvent(release_refresh);
 history::metadata_running=false;SetEvent(history::message_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
 history::enrichment_running=false;SetEvent(history::enrichment_event);assert(WaitForSingleObject(history::enrichment_thread,5000)==WAIT_OBJECT_0);
 fail_allocation=true;bool threw=false;try{history::Tags tags;history::EnrichTags(media,tags);}catch(const std::bad_alloc&){threw=true;}
 assert(threw&&TryAcquireSRWLockExclusive(&history::cache_lock));ReleaseSRWLockExclusive(&history::cache_lock);
 puts("PASS: heartbeat and basic metadata survive blocked enrichment, stale enrichment never replaces newer data and allocation failures release cache locks");
}
