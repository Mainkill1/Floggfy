#define WIN32_LEAN_AND_MEAN
#include "metadata_bridge.h"
#include "cef_identity.h"
#include "hook_init_state.h"
#include "hook_installation.h"
#include "hook_rollback.h"
#include "hook_startup_minhook.h"
#include "rich_metadata.h"
#include "playback_quality.h"
#include "cached_metadata.h"
#include "album_artwork.h"
#include "image_info.h"
#include "history_settings.h"
#include "srw_guard.h"
#include "vendor/minhook/include/MinHook.h"
#include "../build/metadata_script.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
namespace history { namespace {
struct Base {size_t size;void(*add)(Base*);int(*release)(Base*);int(*one)(Base*);int(*any)(Base*);};
struct String {wchar_t* str;size_t length;void(*dtor)(wchar_t*);};
template<size_t N> struct Object {Base base;void* methods[N];};
using Client=Object<19>;using Display=Object<13>;using Load=Object<4>;using Browser=Object<21>;using Frame=Object<26>;
static void Release(void* p) {if(p)static_cast<Base*>(p)->release(static_cast<Base*>(p));}
// CEF's C++ wrapper has private identity fields before the public C structure.
// Replacing that structure breaks GetClient() round trips (CEF UnwrapDerived).
// Intercept callback code instead, preserving every object and its references.
struct CallbackHook {
 SRWLOCK lock=SRWLOCK_INIT;
 std::atomic<void*> target{nullptr},original{nullptr};
 std::atomic<bool> enabled{false};
 bool Install(void* address,void* callback,const char* name) {
  if(!address)return false;
  if(enabled.load(std::memory_order_acquire))return target.load()==address;
  if(!TryAcquireSRWLockExclusive(&lock))return false;
  auto installed=target.load();MH_STATUS status=MH_OK;
  if(installed&&installed!=address){ReleaseSRWLockExclusive(&lock);return false;}
  if(!installed){
   void* trampoline=nullptr;status=MH_CreateHook(address,callback,&trampoline);
   if(status==MH_OK){original.store(trampoline,std::memory_order_release);target.store(address,std::memory_order_release);}
  }
  // MinHook suspends other threads. Never hold a callback lock while enabling.
  ReleaseSRWLockExclusive(&lock);
  if(status==MH_OK)status=MH_EnableHook(address);
  if(status==MH_OK||status==MH_ERROR_ENABLED){enabled.store(true,std::memory_order_release);return true;}
  char line[180];snprintf(line,sizeof(line),"metadata callback %s unavailable: %s",name,MH_StatusToString(status));HistoryLog(line);
  return false;
 }
 template<class F> F Original()const{return reinterpret_cast<F>(original.load(std::memory_order_acquire));}
};
static CallbackHook display_getter,load_getter,console_callback,loading_callback,load_end_callback;
struct Task {Base base;void(*execute)(Task*);};
struct PollTask {Task task;std::atomic<unsigned> refs{1};};
static Frame* polling_frame=nullptr;static SRWLOCK frame_lock=SRWLOCK_INIT;
static std::atomic<bool> poll_pending{false};static int(*post_task)(int,Task*)=nullptr;
static void PollAdd(Base* b){++reinterpret_cast<PollTask*>(b)->refs;}
static int PollDrop(Base* b){auto t=reinterpret_cast<PollTask*>(b);if(--t->refs)return 0;delete t;return 1;}
static int PollOne(Base* b){return reinterpret_cast<PollTask*>(b)->refs==1;}
static int PollAny(Base* b){return reinterpret_cast<PollTask*>(b)->refs>0;}
static std::wstring CollectorOptions() {
 const auto settings=GetSettings();
 return std::wstring(L"window.__floggfyConfig={metadata:")+(settings.metadata?L"true":L"false")+L",lyrics:"+(settings.lyrics?L"true":L"false")+L"};";
}
static void PollExecute(Task*) {
 Frame* frame=nullptr;
 {SharedSrw guard(frame_lock);frame=polling_frame;if(frame)frame->base.add(&frame->base);}
 struct Finish {Frame* frame;~Finish(){Release(frame);poll_pending=false;}} finish{frame};
 try {
  if(!frame || !reinterpret_cast<int(*)(Frame*)>(frame->methods[0])(frame))return;
  const auto code_text=CollectorOptions()+L"if(window.__floggfyPoll)window.__floggfyPoll();";
  String code={const_cast<wchar_t*>(code_text.c_str()),code_text.size(),nullptr};
  const wchar_t name[]=L"floggfy-metadata.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
  reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(frame->methods[14])(frame,&code,&source,1);
 }catch(...) {/* A failed renderer poll must allow the next heartbeat. */}
}
static void SchedulePoll(){
 if(!post_task||poll_pending.exchange(true))return;
 auto t=new(std::nothrow) PollTask;if(!t){poll_pending=false;return;}
 t->task.base={sizeof(Task),PollAdd,PollDrop,PollOne,PollAny};t->task.execute=PollExecute;
 if(!post_task(0,&t->task))poll_pending=false; // The public CEF function consumes the task reference.
}
static MetadataCache cache;static SRWLOCK cache_lock=SRWLOCK_INIT,message_lock=SRWLOCK_INIT;
static hooks::InitController metadata_init;
static hooks::CallbackCounter metadata_callbacks;
static std::atomic<bool> metadata_running{false},metadata_polling{false};
static hooks::QueuedHookSet<3> metadata_startup_hooks;
static hooks::InstallCounts metadata_startup_counts;
static HANDLE metadata_preparation_thread=nullptr;
static bool metadata_prepared=false;
struct Message { ULONGLONG time=0; char data[131073];size_t length=0;};
static std::array<Message,4> messages;static size_t head=0,tail=0,count=0;static HANDLE message_event=nullptr;
static bool Enqueue(const String* value) {
 constexpr wchar_t prefix[]=L"FLOGGFY_METADATA_V1:";constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!value||value->length<=n||value->length>n+131072||wmemcmp(value->str,prefix,n))return false;
 if(TryAcquireSRWLockExclusive(&message_lock)) {
  if(count<messages.size()) {
   auto& m=messages[tail];m.time=GetTickCount64();m.length=value->length-n;bool valid=true;
   for(size_t i=0;i<m.length;i++){wchar_t c=value->str[n+i];if(c>127){valid=false;break;}m.data[i]=char(c);}
   if(valid){tail=(tail+1)%messages.size();++count;SetEvent(message_event);}
  } ReleaseSRWLockExclusive(&message_lock);
 } return true;
}
// A single bounded storage lane owns cache scans and HTTPS artwork fetches.
// The poll/message lane only parses and publishes current client data.
struct EnrichmentJob {RichMetadata metadata;unsigned generation=0;};
static SRWLOCK enrichment_lock=SRWLOCK_INIT;
static std::array<EnrichmentJob,4> enrichment_jobs;
static size_t enrichment_head=0,enrichment_count=0;
static HANDLE enrichment_event=nullptr,enrichment_thread=nullptr;
static std::atomic<bool> enrichment_running{false};
static void MergeAvailableFields(const RichMetadata& available,RichMetadata& live) {
 CachedTrackMetadata cached;
 cached.uri=available.uri;cached.title=available.title;cached.album=available.album;
 cached.duration=available.duration;cached.fields=available.fields;
 // Uses the established missing-field/richer-date rules: current live values
 // win, while unchanged messages retain already obtained enrichment.
 MergeCachedMetadata(cached,live);
}

static DWORD WINAPI EnrichmentWorker(LPVOID) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 while(enrichment_running.load(std::memory_order_acquire)) {
  EnrichmentJob job;bool got=false;
  {ExclusiveSrw guard(enrichment_lock);
   if(enrichment_count){std::swap(job,enrichment_jobs[enrichment_head]);enrichment_head=(enrichment_head+1)%enrichment_jobs.size();--enrichment_count;got=true;}}
  if(!got){WaitForSingleObject(enrichment_event,1000);continue;}
  try {
   auto& metadata=job.metadata;
   auto preferences=GetSettings();
   if(!preferences.metadata || preferences.generation!=job.generation)continue;
   try {EnrichStoredMetadata(metadata);}catch(...){HistoryLog("cached metadata unavailable; ordinary tags retained");}
   if(!metadata.artwork_url.empty()) {
    try {CacheAlbumArtwork(metadata.uri,metadata.artwork_url);}catch(...){HistoryLog("album artwork unavailable; media thumbnail retained");}
   }
   preferences=GetSettings();
   if(!preferences.metadata || preferences.generation!=job.generation)continue;
   if(!preferences.lyrics)metadata.fields.erase("LYRICS");
   {ExclusiveSrw guard(cache_lock);
    const auto current=cache.Find(metadata.title,metadata.artist,metadata.album,metadata.duration);
    if(current && current->uri==metadata.uri) {
     auto merged=*current;MergeAvailableFields(metadata,merged);
     if(!preferences.lyrics)merged.fields.erase("LYRICS");
     cache.Put(merged);
    }}
  }catch(...) {HistoryLog("metadata enrichment failed; current client metadata retained");}
 }
 return 0;
}
static bool StartEnrichmentWorker() {
 // Initialization and all calls originate on the one poll/message worker.
 // Resources live for the process, including conservative hook rollback paths.
 if(enrichment_thread)return true;
 enrichment_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 if(!enrichment_event)return false;
 enrichment_running=true;
 enrichment_thread=CreateThread(nullptr,0,EnrichmentWorker,nullptr,0,nullptr);
 if(!enrichment_thread){enrichment_running=false;CloseHandle(enrichment_event);enrichment_event=nullptr;return false;}
 return true;
}
static void QueueEnrichment(const RichMetadata& metadata,unsigned generation) {
 if(!enrichment_thread)return;
 EnrichmentJob job{metadata,generation}; // Prepare allocating fields before locking.
 {ExclusiveSrw guard(enrichment_lock);
  // New data supersedes pending work for that exact URI.
  for(size_t i=0;i<enrichment_count;++i) {
   const auto index=(enrichment_head+i)%enrichment_jobs.size();
   if(enrichment_jobs[index].metadata.uri==metadata.uri){std::swap(job,enrichment_jobs[index]);SetEvent(enrichment_event);return;}
  }
  if(enrichment_count==enrichment_jobs.size()){std::swap(job,enrichment_jobs[enrichment_head]);enrichment_head=(enrichment_head+1)%enrichment_jobs.size();}
  else {std::swap(job,enrichment_jobs[(enrichment_head+enrichment_count)%enrichment_jobs.size()]);++enrichment_count;}}
 SetEvent(enrichment_event);
}
static DWORD WINAPI MetadataWorker(LPVOID) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 StartEnrichmentWorker();
 Message message;
 while(metadata_running.load(std::memory_order_acquire)) {
  if(metadata_polling.load(std::memory_order_acquire))SchedulePoll();
  bool got=false;
  {ExclusiveSrw guard(message_lock);
   if(count){message=messages[head];head=(head+1)%messages.size();--count;got=true;}}
  if(!got){WaitForSingleObject(message_event,500);continue;}
  try {
   const auto preferences=GetSettings();if(!preferences.metadata)continue;
   RichMetadata metadata;std::string error;
   if(!ParseRichMetadata(std::string(message.data,message.length),metadata,error))continue;
   metadata.quality_time=message.time;
   {ExclusiveSrw guard(cache_lock);
    const auto previous=cache.Find(metadata.title,metadata.artist,metadata.album,metadata.duration);
    if(previous && previous->uri==metadata.uri)MergeAvailableFields(*previous,metadata);
    if(!preferences.lyrics)metadata.fields.erase("LYRICS");
    cache.Put(metadata);}
   QueueEnrichment(metadata,preferences.generation);
  }catch(...) {HistoryLog("metadata message failed; next client snapshot will retry");}
 }
 return 0;
}
static bool CurrentPlayback(const String* text) {
 constexpr wchar_t prefix[]=L"FLOGGFY_PLAYBACK_V1:";
 constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!text || !text->str || text->length<n || wmemcmp(text->str,prefix,n))return false;
 // Bounded cached-state message only: no file reads, enrichment or API calls.
 try {
  PlaybackQualitySnapshot snapshot;
  if(text->length<=n+4096) {
   std::string payload;payload.reserve(text->length-n);
   for(size_t i=n;i<text->length;++i) {
    if(text->str[i]>127){PublishClientPlaybackQuality({});return true;}
    payload+=char(text->str[i]);
   }
   RichMetadata record;std::string error;
   if(ParsePlaybackMetadata(payload,record,error)) {
    // Episodes can share a title and omit music tags. Keep their URI identity
    // separate from SMTC's music key; that cannot identify an episode decoder.
    snapshot.identity=PlaybackIdentity(record.uri.rfind("spotify:episode:",0)==0
        ? record.uri : record.title+"\x1f"+record.artist+"\x1f"+record.album);
    snapshot.level=ParsePlaybackLevel(record.playback_quality);
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,record.title.data(),int(record.title.size()),nullptr,0);
    if(length>0 && length<=4096) {
     std::wstring title(size_t(length),L'\0');
     MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,record.title.data(),int(record.title.size()),title.data(),length);
     auto count=std::min<size_t>(title.size(),snapshot.title.size()-1);
     if(count && title[count-1]>=0xd800 && title[count-1]<=0xdbff)--count;
     std::copy_n(title.begin(),count,snapshot.title.begin());
    }
   }
  }
  PublishClientPlaybackQuality(snapshot);
 }catch(...){PublishClientPlaybackQuality({});}
 return true;
}
static int Console(Display* self,Browser* b,int level,const String* text,const String* source,int line) {
 auto callback=metadata_callbacks.Enter();
 if(CurrentPlayback(text)){Release(b);return 1;}
 if(text&&text->length<100&&text->length>=15&&wmemcmp(text->str,L"FLOGGFY_STATUS:",15)==0){
  std::wstring status(text->str,text->length);HistoryLog(Utf8(status).c_str());Release(b);return 1;
 }
 if(Enqueue(text)){Release(b);return 1;}
 return console_callback.Original<int(*)(Display*,Browser*,int,const String*,const String*,int)>()(self,b,level,text,source,line);
}
static void Inject(Frame* frame) {
 if(!frame||frame->base.size!=sizeof(Frame))return;
 auto main=reinterpret_cast<int(*)(Frame*)>(frame->methods[15]);
 if(!main||!main(frame))return;
 frame->base.add(&frame->base);Frame* old=nullptr;
 {ExclusiveSrw guard(frame_lock);old=polling_frame;polling_frame=frame;}Release(old);
 const auto code_text=CollectorOptions()+metadata_script;
 String code={const_cast<wchar_t*>(code_text.c_str()),code_text.size(),nullptr};
 const wchar_t name[]=L"floggfy-metadata.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
 reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(frame->methods[14])(frame,&code,&source,1);
 HistoryLog("metadata script injected into main frame");
}
static void Loading(Load* self,Browser* b,int loading,int back,int forward){
 auto callback=metadata_callbacks.Enter();
 if(!loading&&b&&b->base.size==sizeof(Browser)){auto frame=reinterpret_cast<Frame*(*)(Browser*)>(b->methods[14])(b);Inject(frame);Release(frame);}
 loading_callback.Original<void(*)(Load*,Browser*,int,int,int)>()(self,b,loading,back,forward);
}
static void LoadEnd(Load* self,Browser* b,Frame* f,int status){
 auto callback=metadata_callbacks.Enter();Inject(f);
 load_end_callback.Original<void(*)(Load*,Browser*,Frame*,int)>()(self,b,f,status);
}
static Display* GetDisplay(Client* self){
 auto callback=metadata_callbacks.Enter();auto result=display_getter.Original<Display*(*)(Client*)>()(self);
 if(result&&result->base.size==sizeof(Display))console_callback.Install(result->methods[6],reinterpret_cast<void*>(Console),"console");
 return result;
}
static Load* GetLoad(Client* self){
 auto callback=metadata_callbacks.Enter();auto result=load_getter.Original<Load*(*)(Client*)>()(self);
 if(result&&result->base.size==sizeof(Load)){
  loading_callback.Install(result->methods[0],reinterpret_cast<void*>(Loading),"loading");
  load_end_callback.Install(result->methods[2],reinterpret_cast<void*>(LoadEnd),"load end");
 }
 return result;
}
static Client* ObserveClient(Client* client){
 if(!client||client->base.size!=sizeof(Client)){HistoryLog("metadata client ABI unsupported; collector not attached");return client;}
 const bool display=display_getter.Install(client->methods[4],reinterpret_cast<void*>(GetDisplay),"display getter");
 const bool load=load_getter.Install(client->methods[14],reinterpret_cast<void*>(GetLoad),"load getter");
 HistoryLog(display&&load?"metadata browser callbacks attached; original client preserved":"metadata browser callbacks unavailable; original client preserved");
 return client;
}
using Create=int(*)(const void*,Client*,const String*,const void*,void*,void*);
using Sync=Browser*(*)(const void*,Client*,const String*,const void*,void*,void*);
using View=void*(*)(Client*,const String*,const void*,void*,void*,void*);
static Create original_create;static Sync original_sync;static View original_view;
static int CreateHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_create(win,ObserveClient(client),url,settings,extra,context);}
static Browser* SyncHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_sync(win,ObserveClient(client),url,settings,extra,context);}
static void* ViewHook(Client* client,const String* url,const void* settings,void* extra,void* context,void* delegate){auto callback=metadata_callbacks.Enter();return original_view(ObserveClient(client),url,settings,extra,context,delegate);}
}
static bool ReadCachedMetadata(const Media& media,RichMetadata& metadata) {
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 SharedSrw guard(cache_lock);
 const auto record=cache.Find(title,artist,album,media.duration);
 if(!record)return false;
 metadata=*record;
 return true;
}
std::string ReadClientPlaybackQuality(const Media& media) {
 if(!GetSettings().metadata)return {};
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 SharedSrw guard(cache_lock);
 const auto record=cache.Find(title,artist,album,media.duration);
 const auto now=GetTickCount64();
 return record && record->quality_time && now>=record->quality_time && now-record->quality_time<=20000 ? record->playback_quality : std::string{};
}
static std::wstring MetadataWide(const std::string& text) {
 if(text.empty()||text.size()>4096)return {};
 const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
 if(!length)return {};
 std::wstring result(size_t(length),L'\0');
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),length)!=length)return {};
 return result;
}
static unsigned MetadataNumber(const std::string& text) {
 if(text.empty()||text.size()>6)return 0;
 unsigned number=0;
 for(char digit:text){if(digit<'0'||digit>'9')return 0;number=number*10+unsigned(digit-'0');}
 return number;
}
void EnrichCatalog(const Media& media,Catalog& catalog) {
 if(!GetSettings().metadata)return;
 const auto title=Utf8(media.title),artist=Utf8(media.artist),album=Utf8(media.album);
 const char* keys[]={"TITLE","ALBUM","ALBUMARTIST","ARTIST","TRACKNUMBER","DISCNUMBER"};
 std::array<std::string,6> fields;std::string uri;
 {SharedSrw guard(cache_lock);
  const auto record=cache.Find(title,artist,album,media.duration);
  if(!record)return;
  uri=record->uri;
  for(size_t i=0;i<fields.size();++i){const auto value=record->fields.find(keys[i]);if(value!=record->fields.end())fields[i]=value->second;}}
 // Decode and allocate the complete replacement outside the cache lock. Keep
 // publication tags and the Song probe on the same canonical directory identity.
 auto next=catalog;next.spotify_uri=std::move(uri);
 auto replace=[](std::wstring& target,const std::string& value){auto wide=MetadataWide(value);if(!wide.empty())target.swap(wide);};
 replace(next.title,fields[0]);replace(next.album,fields[1]);
 if(!fields[2].empty())replace(next.artist,fields[2]);
 else if(media.album_artist.empty())replace(next.artist,fields[3]);
 const auto track=MetadataNumber(fields[4]),disc=MetadataNumber(fields[5]);
 if(track)next.track=track;
 if(disc)next.disc=disc;
 if(next.kind==MediaKind::Music||next.kind==MediaKind::VariousArtists)
  next.kind=_wcsicmp(next.artist.c_str(),L"Various Artists")==0 ? MediaKind::VariousArtists : MediaKind::Music;
 catalog=std::move(next);
}
void EnrichTags(const Media& media,Tags& tags) {
 const auto preferences=GetSettings();if(!preferences.metadata)return;
 RichMetadata metadata;if(!ReadCachedMetadata(media,metadata))return;
 for(const auto& field:metadata.fields) {
  if(field.first=="FLOGGFY_ARTWORK_SOURCE")continue;
  if(field.first=="LYRICS"&&!preferences.lyrics)continue;
  tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& entry){return entry.first==field.first;}),tags.fields.end());
  tags.fields.push_back(field);
 }
 if(!metadata.uri.empty()) {
  std::vector<uint8_t> cover;std::string mime;ImageInfo original;
  if(GetAlbumArtwork(metadata.uri,cover,mime)&&ReadImageInfo(cover,original)) {
   tags.cover=std::move(cover);tags.mime=original.mime;
   // Provenance belongs to the validated client image actually selected above,
   // never to a cached text record or an unchanged Windows thumbnail fallback.
   tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[](const auto& field){return field.first=="FLOGGFY_ARTWORK_SOURCE";}),tags.fields.end());
   tags.fields.push_back({"FLOGGFY_ARTWORK_SOURCE","spotify-client"});
   HistoryLog(("artwork source=album width="+std::to_string(original.width)+" height="+std::to_string(original.height)).c_str());
  }
 }
}
static void FinishMetadataStartup(bool applied,bool activation_attempted) {
 if(!metadata_prepared)return;
 metadata_prepared=false;
 const auto now=static_cast<std::uint64_t>(GetTickCount64());
 if(applied){
  metadata_polling.store(true,std::memory_order_release);SetEvent(message_event);
  CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;metadata_init.Activate();
  char line[160];snprintf(line,sizeof(line),"metadata collector active: found=%u created=%u enabled=%u",metadata_startup_counts.found,metadata_startup_counts.created,metadata_startup_counts.created);HistoryLog(line);
  return;
 }
 // ApplyQueued can return after enabling only a prefix and resuming threads.
 // A thread may be between detour entry and its callback counter. After an
 // activation attempt, clear queued intent and disable roots but retain every
 // trampoline and callback resource for the process lifetime.
 const auto rollback=hooks::RollbackStartupHooks(metadata_startup_hooks,[]{
  for(unsigned waited=0;metadata_callbacks.Active()&&waited<5000;++waited)Sleep(1);
  return metadata_callbacks.Active()==0;
 },!activation_attempted);
 // A browser created during a partial apply can install persistent secondary
 // hooks. Keep the worker/event and callback state alive for those hooks.
 const bool dependents=display_getter.target.load()||load_getter.target.load()||console_callback.target.load()||loading_callback.target.load()||load_end_callback.target.load();
 if(!activation_attempted&&rollback.CanRelease()&&!dependents){
  metadata_polling.store(false,std::memory_order_release);metadata_running.store(false,std::memory_order_release);SetEvent(message_event);
  const bool stopped=WaitForSingleObject(metadata_preparation_thread,5000)==WAIT_OBJECT_0;
  CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;
  if(stopped){CloseHandle(message_event);message_event=nullptr;HistoryLog("metadata startup activation failed; rollback complete; retry scheduled");metadata_init.Retry(now);return;}
 }
 if(metadata_preparation_thread){CloseHandle(metadata_preparation_thread);metadata_preparation_thread=nullptr;}
 HistoryLog("metadata startup activation failed or rollback unsafe; trampoline and worker state preserved and retries disabled");metadata_init.MarkUnsupported();
}
void FinalizeMetadataCollector(bool applied) {FinishMetadataStartup(applied,true);}
bool PrepareMetadataCollector(HMODULE cef) {
 const auto now=static_cast<std::uint64_t>(GetTickCount64());
 if(!metadata_init.TryBegin(now))return false;
 if(!GetSettings().metadata){HistoryLog("metadata collector disabled by INI Metadata=0");metadata_init.MarkUnsupported();return false;}
 auto address=GetProcAddress(cef,"cef_version_info");int(*version)(int)=nullptr;static_assert(sizeof(version)==sizeof(address));memcpy(&version,&address,sizeof(version));
 if(!version||!cef_compat::IsSupported({version(0),version(1),version(2),version(3)})){
  HistoryLog("metadata CEF identity unsupported; revision is not in the audited compatibility table");metadata_init.MarkUnsupported();return false;
 }
 auto post_address=GetProcAddress(cef,"cef_post_task");memcpy(&post_task,&post_address,sizeof(post_task));
 if(!post_task){HistoryLog("metadata collector unsupported: cef_post_task missing");metadata_init.MarkUnsupported();return false;}
 message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 if(!message_event){HistoryLog("metadata event allocation failed; retry scheduled");metadata_init.Retry(now);return false;}
 head=tail=count=0;
 metadata_polling.store(false,std::memory_order_release);metadata_running.store(true,std::memory_order_release);
 metadata_preparation_thread=CreateThread(nullptr,0,MetadataWorker,nullptr,0,nullptr);
 if(!metadata_preparation_thread){metadata_running.store(false,std::memory_order_release);CloseHandle(message_event);message_event=nullptr;HistoryLog("metadata worker startup failed; retry scheduled");metadata_init.Retry(now);return false;}
 metadata_prepared=true;metadata_startup_counts={};
 auto status=MH_Initialize();if(status==MH_ERROR_ALREADY_INITIALIZED)status=MH_OK;
 if(status!=MH_OK){HistoryLog("metadata MinHook initialization failed");FinishMetadataStartup(false,false);return false;}
 const char* names[]={"cef_browser_host_create_browser","cef_browser_host_create_browser_sync","cef_browser_view_create"};
 void* callbacks[]={reinterpret_cast<void*>(CreateHook),reinterpret_cast<void*>(SyncHook),reinterpret_cast<void*>(ViewHook)};
 void** originals[]={reinterpret_cast<void**>(&original_create),reinterpret_cast<void**>(&original_sync),reinterpret_cast<void**>(&original_view)};
 bool complete=true;
 for(unsigned i=0;i<3;i++){
  auto target=reinterpret_cast<void*>(GetProcAddress(cef,names[i]));if(!target)continue;
  metadata_startup_counts.Found();status=MH_CreateHook(target,callbacks[i],originals[i]);metadata_startup_counts.Created(status==MH_OK);
  if(status!=MH_OK){char line[200];snprintf(line,sizeof(line),"metadata hook %s create failed: %s",names[i],MH_StatusToString(status));HistoryLog(line);complete=false;continue;}
  metadata_startup_hooks.Add(target);
 }
 if(!complete||!metadata_startup_hooks.Size()||!hooks::QueueStartupHooks(metadata_startup_hooks)){
  HistoryLog("metadata startup hook preparation unavailable");FinishMetadataStartup(false,false);return false;
 }
 return true;
}
void StartMetadataCollector(HMODULE cef) {
 if(PrepareMetadataCollector(cef))FinalizeMetadataCollector(MH_ApplyQueued()==MH_OK);
}
}
