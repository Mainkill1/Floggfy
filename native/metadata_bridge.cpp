#define WIN32_LEAN_AND_MEAN
#include "metadata_bridge.h"
#include "cef_identity.h"
#include "hook_init_state.h"
#include "hook_installation.h"
#include "hook_rollback.h"
#include "rich_metadata.h"
#include "history_settings.h"
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
static void PollExecute(Task*){
 Frame* f=nullptr;AcquireSRWLockShared(&frame_lock);f=polling_frame;if(f)f->base.add(&f->base);ReleaseSRWLockShared(&frame_lock);
 if(f&&reinterpret_cast<int(*)(Frame*)>(f->methods[0])(f)){
  const wchar_t code_text[]=L"if(window.__floggfyPoll)window.__floggfyPoll();";
  String code={const_cast<wchar_t*>(code_text),wcslen(code_text),nullptr};const wchar_t name[]=L"floggfy-metadata.js";String source={const_cast<wchar_t*>(name),wcslen(name),nullptr};
  reinterpret_cast<void(*)(Frame*,const String*,const String*,int)>(f->methods[14])(f,&code,&source,1);
 }
 Release(f);poll_pending=false;
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
struct Message {char data[131073];size_t length=0;};
static std::array<Message,4> messages;static size_t head=0,tail=0,count=0;static HANDLE message_event=nullptr;
static bool Enqueue(const String* value) {
 constexpr wchar_t prefix[]=L"FLOGGFY_METADATA_V1:";constexpr size_t n=sizeof(prefix)/sizeof(*prefix)-1;
 if(!value||value->length<=n||value->length>n+131072||wmemcmp(value->str,prefix,n))return false;
 if(TryAcquireSRWLockExclusive(&message_lock)) {
  if(count<messages.size()) {
   auto& m=messages[tail];m.length=value->length-n;bool valid=true;
   for(size_t i=0;i<m.length;i++){wchar_t c=value->str[n+i];if(c>127){valid=false;break;}m.data[i]=char(c);}
   if(valid){tail=(tail+1)%messages.size();++count;SetEvent(message_event);}
  } ReleaseSRWLockExclusive(&message_lock);
 } return true;
}
static DWORD WINAPI MetadataWorker(LPVOID) {
 Message m;
 while(metadata_running.load(std::memory_order_acquire)){if(metadata_polling.load(std::memory_order_acquire))SchedulePoll();bool got=false;AcquireSRWLockExclusive(&message_lock);if(count){m=messages[head];head=(head+1)%messages.size();--count;got=true;}ReleaseSRWLockExclusive(&message_lock);
  if(!got){WaitForSingleObject(message_event,1000);continue;}
  RichMetadata metadata;std::string error;if(!ParseRichMetadata(std::string(m.data,m.length),metadata,error))continue;
  AcquireSRWLockExclusive(&cache_lock);cache.Put(metadata);ReleaseSRWLockExclusive(&cache_lock);
  HistoryLog(("metadata cached fields="+std::to_string(metadata.fields.size())+" date="+(metadata.fields.count("DATE")?metadata.fields.at("DATE"):std::string{})+" lyrics_bytes="+std::to_string((metadata.fields.count("LYRICS")?metadata.fields.at("LYRICS").size():0))).c_str());
 }
 return 0;
}
static int Console(Display* self,Browser* b,int level,const String* text,const String* source,int line) {
 auto callback=metadata_callbacks.Enter();
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
 frame->base.add(&frame->base);AcquireSRWLockExclusive(&frame_lock);auto old=polling_frame;polling_frame=frame;ReleaseSRWLockExclusive(&frame_lock);Release(old);
 String code={const_cast<wchar_t*>(metadata_script),wcslen(metadata_script),nullptr};
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
void EnrichTags(const Media& media,Tags& tags) {
 if(!GetSettings().metadata)return;
 AcquireSRWLockShared(&cache_lock);
 auto m=cache.Find(Utf8(media.title),Utf8(media.artist),Utf8(media.album),media.duration);
 if(m)for(const auto& field:m->fields) {
  tags.fields.erase(std::remove_if(tags.fields.begin(),tags.fields.end(),[&](const auto& entry){return entry.first==field.first;}),tags.fields.end());
  tags.fields.push_back(field);
 }
 ReleaseSRWLockShared(&cache_lock);
}
void StartMetadataCollector(HMODULE cef) {
 const auto now=static_cast<std::uint64_t>(GetTickCount64());
 if(!metadata_init.TryBegin(now))return;
 if(!GetSettings().metadata){HistoryLog("metadata collector disabled by INI Metadata=0");metadata_init.MarkUnsupported();return;}
 auto address=GetProcAddress(cef,"cef_version_info");int(*version)(int)=nullptr;static_assert(sizeof(version)==sizeof(address));memcpy(&version,&address,sizeof(version));
 if(!version||!cef_compat::IsSupported({version(0),version(1),version(2),version(3)})){
  HistoryLog("metadata CEF identity unsupported; revision is not in the audited compatibility table");metadata_init.MarkUnsupported();return;
 }
 auto post_address=GetProcAddress(cef,"cef_post_task");memcpy(&post_task,&post_address,sizeof(post_task));
 if(!post_task){HistoryLog("metadata collector unsupported: cef_post_task missing");metadata_init.MarkUnsupported();return;}
 message_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 if(!message_event){HistoryLog("metadata event allocation failed; retry scheduled");metadata_init.Retry(now);return;}
 head=tail=count=0;
 metadata_polling.store(false,std::memory_order_release);metadata_running.store(true,std::memory_order_release);
 HANDLE thread=CreateThread(nullptr,0,MetadataWorker,nullptr,0,nullptr);
 if(!thread){metadata_running.store(false,std::memory_order_release);CloseHandle(message_event);message_event=nullptr;HistoryLog("metadata worker startup failed; retry scheduled");metadata_init.Retry(now);return;}
 auto stop_worker=[&] {
  metadata_polling.store(false,std::memory_order_release);metadata_running.store(false,std::memory_order_release);SetEvent(message_event);
  const bool stopped=WaitForSingleObject(thread,5000)==WAIT_OBJECT_0;CloseHandle(thread);thread=nullptr;
  if(stopped){CloseHandle(message_event);message_event=nullptr;}return stopped;
 };
 auto status=MH_Initialize();if(status==MH_ERROR_ALREADY_INITIALIZED)status=MH_OK;
 if(status!=MH_OK){if(stop_worker()){HistoryLog("metadata MinHook initialization failed; retry scheduled");metadata_init.Retry(now);}else{HistoryLog("metadata worker did not quiesce; state preserved");metadata_init.MarkUnsupported();}return;}
 const char* names[]={"cef_browser_host_create_browser","cef_browser_host_create_browser_sync","cef_browser_view_create"};
 void* callbacks[]={reinterpret_cast<void*>(CreateHook),reinterpret_cast<void*>(SyncHook),reinterpret_cast<void*>(ViewHook)};
 void** originals[]={reinterpret_cast<void**>(&original_create),reinterpret_cast<void**>(&original_sync),reinterpret_cast<void**>(&original_view)};
 void* created_targets[3]={};unsigned created_count=0;hooks::InstallCounts counts;
 for(unsigned i=0;i<3;i++){
  auto target=reinterpret_cast<void*>(GetProcAddress(cef,names[i]));if(!target)continue;
  counts.Found();status=MH_CreateHook(target,callbacks[i],originals[i]);counts.Created(status==MH_OK);
  if(status!=MH_OK){char line[200];snprintf(line,sizeof(line),"metadata hook %s create failed: %s",names[i],MH_StatusToString(status));HistoryLog(line);continue;}
  created_targets[created_count]=target;
  status=MH_EnableHook(target);counts.Enabled(status==MH_OK);
  ++created_count;
  if(status!=MH_OK){char line[200];snprintf(line,sizeof(line),"metadata hook %s enable failed: %s",names[i],MH_StatusToString(status));HistoryLog(line);continue;}
 }
 if(!counts.Usable()){
  char line[180];snprintf(line,sizeof(line),"metadata collector unavailable: found=%u created=%u enabled=%u; retry scheduled",counts.found,counts.created,counts.enabled);HistoryLog(line);
  hooks::RollbackStatus rollback;
  auto disabled=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_DISABLED||value==MH_ERROR_NOT_CREATED;};
  auto removed=[](MH_STATUS value){return value==MH_OK||value==MH_ERROR_NOT_CREATED;};
  for(unsigned i=0;i<created_count;i++)rollback.ObserveDisable(disabled(MH_DisableHook(created_targets[i])));
  for(unsigned waited=0;metadata_callbacks.Active()&&waited<5000;++waited)Sleep(1);
  rollback.ObserveQuiescence(metadata_callbacks.Active()==0);
  if(rollback.quiescent)for(unsigned i=0;i<created_count;i++)rollback.ObserveRemove(removed(MH_RemoveHook(created_targets[i])));
  if(rollback.CanRelease()&&stop_worker())metadata_init.Retry(now);
  else {if(thread)CloseHandle(thread);HistoryLog("metadata rollback incomplete; callback state preserved and retries disabled");metadata_init.MarkUnsupported();}
  return;
 }
 metadata_polling.store(true,std::memory_order_release);SetEvent(message_event);CloseHandle(thread);metadata_init.Activate();
 char line[160];snprintf(line,sizeof(line),"metadata collector active: found=%u created=%u enabled=%u",counts.found,counts.created,counts.enabled);HistoryLog(line);
}
}
