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
#include <type_traits>
namespace history { namespace {
struct Base {size_t size;void(*add)(Base*);int(*release)(Base*);int(*one)(Base*);int(*any)(Base*);};
struct String {wchar_t* str;size_t length;void(*dtor)(wchar_t*);};
template<size_t N> struct Object {Base base;void* methods[N];};
using Client=Object<19>;using Display=Object<13>;using Load=Object<4>;using Browser=Object<21>;using Frame=Object<26>;
static void Release(void* p) {if(p)static_cast<Base*>(p)->release(static_cast<Base*>(p));}
template<size_t N> struct Wrapper {Object<N> object;std::atomic<unsigned> refs{1};Object<N>* original=nullptr;};
template<size_t N> static Wrapper<N>* W(Object<N>* o){return reinterpret_cast<Wrapper<N>*>(o);}
template<size_t N> static void Add(Base* b){++reinterpret_cast<Wrapper<N>*>(b)->refs;}
template<size_t N> static int Drop(Base* b){auto w=reinterpret_cast<Wrapper<N>*>(b);if(--w->refs)return 0;Release(w->original);delete w;return 1;}
template<size_t N> static int One(Base* b){return reinterpret_cast<Wrapper<N>*>(b)->refs==1;}
template<size_t N> static int Any(Base* b){return reinterpret_cast<Wrapper<N>*>(b)->refs>0;}
template<size_t N> static Wrapper<N>* New(Object<N>* original){auto w=new(std::nothrow) Wrapper<N>;if(w){w->object.base={sizeof(Object<N>),Add<N>,Drop<N>,One<N>,Any<N>};w->original=original;}return w;}
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
template<size_t N,size_t I,class R,class...A> static R Forward(Object<N>* self,A...args) {
 auto original=W(self)->original;
 if(original&&original->methods[I]) {
  auto f=reinterpret_cast<R(*)(Object<N>*,A...)>(original->methods[I]);
  if constexpr(std::is_void_v<R>){f(original,args...);return;}else return f(original,args...);
 }
 if constexpr(!std::is_void_v<R>)return R{};
}
template<size_t I,class R,class...A> static R DisplayForward(Display* self,Browser* browser,A...args) {
 auto original=W(self)->original;
 if(original&&original->methods[I])return Forward<13,I,R>(self,browser,args...);
 Release(browser);if constexpr(!std::is_void_v<R>)return R{};
}
static void Address(Display* self,Browser* b,Frame* f,const String* s){auto o=W(self)->original;if(o&&o->methods[0])Forward<13,0,void>(self,b,f,s);else {Release(b);Release(f);}}
static int Console(Display* self,Browser* b,int level,const String* text,const String* source,int line) {
 if(text&&text->length<100&&text->length>=15&&wmemcmp(text->str,L"FLOGGFY_STATUS:",15)==0){
  std::wstring status(text->str,text->length);HistoryLog(Utf8(status).c_str());Release(b);return 1;
 }
 if(Enqueue(text)){Release(b);return 1;}return DisplayForward<6,int>(self,b,level,text,source,line);
}
static Display* WrapDisplay(Display* original) {
 if(original&&original->base.size!=sizeof(Display)){HistoryLog(("metadata display ABI unsupported bytes="+std::to_string(original->base.size)).c_str());return original;}
 auto w=New(original);if(!w)return original;auto& f=w->object.methods;
 f[0]=reinterpret_cast<void*>(Address);f[1]=reinterpret_cast<void*>(DisplayForward<1,void,const String*>);
 f[2]=reinterpret_cast<void*>(DisplayForward<2,void,void*>);f[3]=reinterpret_cast<void*>(DisplayForward<3,void,int>);
 f[4]=reinterpret_cast<void*>(DisplayForward<4,int,String*>);f[5]=reinterpret_cast<void*>(DisplayForward<5,void,const String*>);
 f[6]=reinterpret_cast<void*>(Console);f[7]=reinterpret_cast<void*>(DisplayForward<7,int,const void*>);
 f[8]=reinterpret_cast<void*>(DisplayForward<8,void,double>);f[9]=reinterpret_cast<void*>(DisplayForward<9,int,void*,int,const void*>);
 f[10]=reinterpret_cast<void*>(DisplayForward<10,void,int,int>);f[11]=reinterpret_cast<void*>(DisplayForward<11,int,const void*>);
 f[12]=reinterpret_cast<void*>(DisplayForward<12,int,void*>);return &w->object;
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
 if(!loading&&b&&b->base.size==sizeof(Browser)){auto frame=reinterpret_cast<Frame*(*)(Browser*)>(b->methods[14])(b);Inject(frame);Release(frame);}
 auto o=W(self)->original;if(o&&o->methods[0])Forward<4,0,void>(self,b,loading,back,forward);else Release(b);
}
static void LoadStart(Load* self,Browser* b,Frame* f,int type){auto o=W(self)->original;if(o&&o->methods[1])Forward<4,1,void>(self,b,f,type);else {Release(b);Release(f);}}
static void LoadEnd(Load* self,Browser* b,Frame* f,int status){Inject(f);auto o=W(self)->original;if(o&&o->methods[2])Forward<4,2,void>(self,b,f,status);else {Release(b);Release(f);}}
static void LoadError(Load* self,Browser* b,Frame* f,int err,const String* text,const String* url){auto o=W(self)->original;if(o&&o->methods[3])Forward<4,3,void>(self,b,f,err,text,url);else {Release(b);Release(f);}}
static Load* WrapLoad(Load* original){if(original&&original->base.size!=sizeof(Load))return original;auto w=New(original);if(!w)return original;w->object.methods[0]=reinterpret_cast<void*>(Loading);w->object.methods[1]=reinterpret_cast<void*>(LoadStart);w->object.methods[2]=reinterpret_cast<void*>(LoadEnd);w->object.methods[3]=reinterpret_cast<void*>(LoadError);return &w->object;}
template<size_t I> static void* Getter(Client* self){return Forward<19,I,void*>(self);}
static Display* GetDisplay(Client* self){return WrapDisplay(static_cast<Display*>(Getter<4>(self)));}
static Load* GetLoad(Client* self){return WrapLoad(static_cast<Load*>(Getter<14>(self)));}
static int Process(Client* self,Browser* b,Frame* f,int source,void* message){auto o=W(self)->original;if(o&&o->methods[18])return Forward<19,18,int>(self,b,f,source,message);Release(b);Release(f);Release(message);return 0;}
static Client* WrapClient(Client* original){
 if(!original||original->base.size!=sizeof(Client)){HistoryLog("metadata client ABI unsupported; collector not attached");return original;}
 auto w=New(original);if(!w)return original;
 void* getters[]={reinterpret_cast<void*>(Getter<0>),reinterpret_cast<void*>(Getter<1>),reinterpret_cast<void*>(Getter<2>),reinterpret_cast<void*>(Getter<3>),reinterpret_cast<void*>(GetDisplay),reinterpret_cast<void*>(Getter<5>),reinterpret_cast<void*>(Getter<6>),reinterpret_cast<void*>(Getter<7>),reinterpret_cast<void*>(Getter<8>),reinterpret_cast<void*>(Getter<9>),reinterpret_cast<void*>(Getter<10>),reinterpret_cast<void*>(Getter<11>),reinterpret_cast<void*>(Getter<12>),reinterpret_cast<void*>(Getter<13>),reinterpret_cast<void*>(GetLoad),reinterpret_cast<void*>(Getter<15>),reinterpret_cast<void*>(Getter<16>),reinterpret_cast<void*>(Getter<17>),reinterpret_cast<void*>(Process)};
 memcpy(w->object.methods,getters,sizeof(getters));HistoryLog("metadata browser client attached");return &w->object;
}
using Create=int(*)(const void*,Client*,const String*,const void*,void*,void*);
using Sync=Browser*(*)(const void*,Client*,const String*,const void*,void*,void*);
using View=void*(*)(Client*,const String*,const void*,void*,void*,void*);
static Create original_create;static Sync original_sync;static View original_view;
static int CreateHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_create(win,WrapClient(client),url,settings,extra,context);}
static Browser* SyncHook(const void* win,Client* client,const String* url,const void* settings,void* extra,void* context){auto callback=metadata_callbacks.Enter();return original_sync(win,WrapClient(client),url,settings,extra,context);}
static void* ViewHook(Client* client,const String* url,const void* settings,void* extra,void* context,void* delegate){auto callback=metadata_callbacks.Enter();return original_view(WrapClient(client),url,settings,extra,context,delegate);}
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
  HistoryLog("metadata CEF identity unsupported; expected 146.0.10 commit 3504");metadata_init.MarkUnsupported();return;
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
