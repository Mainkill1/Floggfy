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
HANDLE blocked=nullptr,release_enrichment=nullptr;
std::atomic<unsigned> polls{0},enrichments{0};
int Post(int,history::Task* task){++polls;task->execute(task);task->base.release(&task->base);return 1;}
void Send(const wchar_t* year){
 std::wstring value=L"FLOGGFY_METADATA_V1:v=1&title=Song&artist=Artist&album=Album&duration=200&uri=spotify%3Atrack%3A0000000000000000000001&playback_quality=lossless&DISCNUMBER=2&DATE="+std::wstring(year);
 history::String text{value.data(),value.size(),nullptr};assert(history::Enqueue(&text));
}
}
namespace history {
void EnrichStoredMetadata(RichMetadata& metadata){
 if(enrichments.fetch_add(1)==0){SetEvent(blocked);WaitForSingleObject(release_enrichment,10000);}
 metadata.fields["COMMENT"]="slow enrichment";
}
void CacheAlbumArtwork(const std::string&,const std::string&){}
bool GetAlbumArtwork(const std::string&,std::vector<uint8_t>&,std::string&){return false;}
}
int main(){
 blocked=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_enrichment=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(blocked&&release_enrichment);
 history::message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);history::post_task=Post;
 history::metadata_running=true;history::metadata_polling=true;
 HANDLE worker=CreateThread(nullptr,0,history::MetadataWorker,nullptr,0,nullptr);assert(worker);
 Send(L"2025");assert(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
 const auto before=polls.load();Sleep(1200);assert(polls.load()>before); // Slow enrichment cannot consume the heartbeat.
 history::Media media;media.title=L"Song";media.artist=L"Artist";media.album=L"Album";media.duration=200;
 assert(history::ReadClientPlaybackQuality(media)=="lossless");
 history::Catalog catalog;history::EnrichCatalog(media,catalog);assert(catalog.disc==2&&catalog.spotify_uri=="spotify:track:0000000000000000000001");
 Send(L"2026");
 history::Tags current;
 const auto deadline=GetTickCount64()+5000;
 for(;;){current={};history::EnrichTags(media,current);if(std::find(current.fields.begin(),current.fields.end(),std::make_pair(std::string("DATE"),std::string("2026")))!=current.fields.end())break;assert(GetTickCount64()<deadline);Sleep(5);}
 auto date=std::find_if(current.fields.begin(),current.fields.end(),[](const auto& item){return item.first=="DATE";});assert(date!=current.fields.end()&&date->second=="2026");
 SetEvent(release_enrichment);while(enrichments.load()<2){assert(GetTickCount64()<deadline);Sleep(5);}
 current={};history::EnrichTags(media,current);date=std::find_if(current.fields.begin(),current.fields.end(),[](const auto& item){return item.first=="DATE";});assert(date!=current.fields.end()&&date->second=="2026");
 fail_allocation=true;bool threw=false;try{history::Tags tags;history::EnrichTags(media,tags);}catch(const std::bad_alloc&){threw=true;}
 assert(threw&&TryAcquireSRWLockExclusive(&history::cache_lock));ReleaseSRWLockExclusive(&history::cache_lock);
 history::metadata_running=false;SetEvent(history::message_event);assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
 puts("PASS: heartbeat and basic metadata survive blocked enrichment, stale enrichment never replaces newer data and allocation failures release cache locks");
}
