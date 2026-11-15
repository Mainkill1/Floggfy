#define WIN32_LEAN_AND_MEAN
#include "telemetry_filter.h"
#include "telemetry_policy.h"
#include "async_log.h"
#include "vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace privacy {
namespace {
// Public CEF prefixes audited against the revisions in vendor/cef/UPSTREAM.md
// and generated 6778 C API glue. Never replace a nonnull Spotify object: its
// private C++ wrapper identity lives before this public structure.
struct Base {size_t size;void(*add)(Base*);int(*release)(Base*);int(*one)(Base*);int(*any)(Base*);};
struct String {wchar_t* str;size_t length;void(*dtor)(wchar_t*);};
template<size_t N> struct Object {Base base;void* methods[N];};
using Client=Object<19>;using Request=Object<23>;using Handler=Object<11>;using Resource=Object<8>;
using Create=void*(*)(Request*,void*,void*);
using GetHandler=Handler*(*)(Client*);
using GetResource=Resource*(*)(Handler*,void*,void*,Request*,int,int,const String*,int*);
using Before=int(*)(Resource*,void*,void*,Request*,void*);
struct LastError {DWORD value=GetLastError();~LastError(){SetLastError(value);}void Restore()const{SetLastError(value);}};
std::atomic<bool> enabled{false},dirty{false},fallback_seen{false};
std::atomic<unsigned> unsupported{0},renderer{0};
std::atomic<unsigned long long> blocked[4]{};
void(*free_string)(String*)=nullptr;

bool Memory(const void* p,size_t bytes,bool executable=false) noexcept {
 if(!p||!bytes)return false;
 auto cursor=reinterpret_cast<uintptr_t>(p);
 if(bytes>UINTPTR_MAX-cursor)return false;
 const auto end=cursor+bytes;
 while(cursor<end){
  MEMORY_BASIC_INFORMATION m{};
  if(!VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto protection=m.Protect&0xff;
  if(executable){if(protection!=PAGE_EXECUTE&&protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;}
  else if(protection!=PAGE_READONLY&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY&&protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;
  const auto next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
  if(next<=cursor)return false;
  cursor=next;
 }
 return true;
}
bool Prefix(const void* p,size_t bytes) noexcept {
 return Memory(p,sizeof(Base))&&static_cast<const Base*>(p)->size>=bytes&&Memory(p,bytes);
}
bool Releasable(void* p) noexcept {
 return !p||(Prefix(p,sizeof(Base))&&Memory(reinterpret_cast<void*>(static_cast<Base*>(p)->release),1,true));
}
void Drop(void* p) noexcept {if(p)static_cast<Base*>(p)->release(static_cast<Base*>(p));}
void Unsupported() noexcept {unsupported.fetch_add(1,std::memory_order_relaxed);dirty=true;}
void Count(Rule rule) noexcept {blocked[static_cast<unsigned>(rule)].fetch_add(1,std::memory_order_relaxed);dirty=true;}
Rule RequestRule(Request* request) noexcept {
 if(!free_string||!Prefix(request,sizeof(Base)+2*sizeof(void*))||!Memory(request->methods[1],1,true)){Unsupported();return Rule::Allow;}
 // CEF member self is borrowed, not transferred. get_url does not consume it.
 auto value=reinterpret_cast<String*(*)(Request*)>(request->methods[1])(request);
 if(!value)return Rule::Allow;
 Rule rule=Rule::Allow;
 if(Memory(value,sizeof(String))&&value->length<=32768&&(!value->length||Memory(value->str,value->length*sizeof(wchar_t))))rule=Classify({value->str,value->length});
 else Unsupported();
 free_string(value);
 return rule;
}
struct CallbackHook {
 SRWLOCK lock=SRWLOCK_INIT;
 std::atomic<void*> target{nullptr},original{nullptr};
 std::atomic<bool> active{false};
 bool Install(void* address,void* callback) noexcept {
  if(!Memory(address,1,true)){Unsupported();return false;}
  if(active.load(std::memory_order_acquire)){if(target.load()==address)return true;Unsupported();return false;}
  if(!TryAcquireSRWLockExclusive(&lock)){Unsupported();return false;}
  auto installed=target.load();MH_STATUS status=MH_OK;
  if(installed&&installed!=address){ReleaseSRWLockExclusive(&lock);Unsupported();return false;}
  if(!installed){void* trampoline=nullptr;status=MH_CreateHook(address,callback,&trampoline);if(status==MH_OK){original.store(trampoline,std::memory_order_release);target.store(address,std::memory_order_release);}}
  // Enabling suspends other threads, so do not hold our callback lock here.
  ReleaseSRWLockExclusive(&lock);
  if(status==MH_OK)status=MH_EnableHook(address);
  if(status==MH_OK||status==MH_ERROR_ENABLED){active.store(true,std::memory_order_release);dirty=true;return true;}
  Unsupported();return false;
 }
 template<class F> F Original()const noexcept{return reinterpret_cast<F>(original.load(std::memory_order_acquire));}
};
CallbackHook create_hook,handler_hook,resource_hook,before_hook;
// Owned exclusively by the startup worker. All callback/trampoline storage
// remains alive even when a shared batch enables only a prefix before failing.
bool preparation_attempted=false,root_prepared=false;

template<class T> struct Owned {
 T api{};std::atomic<unsigned> refs{1};
 static void Add(Base* b){++reinterpret_cast<Owned*>(b)->refs;}
 static int Release(Base* b){auto self=reinterpret_cast<Owned*>(b);if(--self->refs)return 0;delete self;return 1;}
 static int One(Base* b){return reinterpret_cast<Owned*>(b)->refs==1;}
 static int Any(Base* b){return reinterpret_cast<Owned*>(b)->refs>0;}
 Owned(){api.base={sizeof(T),Add,Release,One,Any};}
};
int Cancel(Resource*,void* browser,void* frame,Request* request,void* callback) {
 LastError error;
 // CEF callback refptr parameters are transferred. self remains borrowed.
 Drop(browser);Drop(frame);Drop(request);Drop(callback);
 return 0; // RV_CANCEL
}
Resource* Cancellation() noexcept {
 auto result=new(std::nothrow) Owned<Resource>;
 if(!result){Unsupported();return nullptr;}
 result->api.methods[1]=reinterpret_cast<void*>(Cancel);
 return &result->api;
}
int BeforeLoad(Resource* self,void* browser,void* frame,Request* request,void* callback){
 LastError error;
 if(Enabled()&&self&&request&&callback&&Releasable(browser)&&Releasable(frame)&&Releasable(request)&&Releasable(callback)){
  const auto rule=RequestRule(request);
  if(rule!=Rule::Allow){Count(rule);return Cancel(self,browser,frame,request,callback);}
 }
 error.Restore();const auto result=before_hook.Original<Before>()(self,browser,frame,request,callback);error.value=GetLastError();return result;
}
Resource* ResourceRequest(Handler* self,void* browser,void* frame,Request* request,int navigation,int download,const String* initiator,int* disable);
Resource* DefaultResource(Handler*,void* browser,void* frame,Request* request,int,int,const String*,int*){
 LastError error;
 Resource* result=nullptr;
 if(Enabled()){
  const auto rule=RequestRule(request);
  if(rule!=Rule::Allow){result=Cancellation();if(result)Count(rule);}
 }
 Drop(browser);Drop(frame);Drop(request);
 // Null preserves CEF's request-context handler fallback on allowed routes.
 return result;
}
Handler* DefaultHandler() noexcept {
 auto result=new(std::nothrow) Owned<Handler>;
 if(!result){Unsupported();return nullptr;}
 result->api.methods[2]=reinterpret_cast<void*>(DefaultResource);
 return &result->api;
}
Resource* ResourceRequest(Handler* self,void* browser,void* frame,Request* request,int navigation,int download,const String* initiator,int* disable){
 LastError error;
 if(Enabled()&&self&&browser&&frame&&request&&disable&&Releasable(browser)&&Releasable(frame)&&Releasable(request)){
  const auto rule=RequestRule(request);
  if(rule!=Rule::Allow){if(auto cancel=Cancellation()){Count(rule);Drop(browser);Drop(frame);Drop(request);return cancel;}}
 }
 error.Restore();auto result=resource_hook.Original<GetResource>()(self,browser,frame,request,navigation,download,initiator,disable);error.value=GetLastError();
 // Preserve exact returned handler identity and its transferred reference. A
 // nonnull pre-load callback can also check a URL changed after the first gate.
 if(result&&Prefix(result,sizeof(Base)+2*sizeof(void*))&&result->methods[1])before_hook.Install(result->methods[1],reinterpret_cast<void*>(BeforeLoad));
 else Unsupported(); // No safe interception point for later URL changes.
 return result;
}
Handler* RequestHandler(Client* self){
 LastError error;
 error.Restore();auto result=handler_hook.Original<GetHandler>()(self);error.value=GetLastError();
 if(!Enabled()||!self)return result;
 if(!result){auto fallback=DefaultHandler();if(fallback){fallback_seen=true;dirty=true;}return fallback;}
 if(Prefix(result,sizeof(Base)+3*sizeof(void*)))resource_hook.Install(result->methods[2],reinterpret_cast<void*>(ResourceRequest));
 else Unsupported();
 return result;
}
void* CreateRequest(Request* request,void* client,void* context){
 LastError error;
 if(Enabled()&&request&&client&&Releasable(request)&&Releasable(client)&&Releasable(context)){
  const auto rule=RequestRule(request);
  if(rule!=Rule::Allow){Count(rule);Drop(request);Drop(client);Drop(context);return nullptr;}
 }
 error.Restore();auto result=create_hook.Original<Create>()(request,client,context);error.value=GetLastError();return result;
}
template<class F> F Export(HMODULE module,const char* name) noexcept {
 auto address=GetProcAddress(module,name);F function=nullptr;static_assert(sizeof(function)==sizeof(address));std::memcpy(&function,&address,sizeof(function));return function;
}
} // namespace
bool Enabled() noexcept{return enabled.load(std::memory_order_acquire);}
bool Prepare(HMODULE cef,bool preference) noexcept {
 LastError error;
 if(preparation_attempted)return false;
 preparation_attempted=true;
 enabled.store(preference,std::memory_order_release);dirty=true;
 if(!preference)return false;
 if(!cef){Unsupported();return false;}
 free_string=Export<decltype(free_string)>(cef,"cef_string_userfree_utf16_free");
 if(!Memory(reinterpret_cast<void*>(free_string),1,true)){free_string=nullptr;Unsupported();return false;}
 const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){Unsupported();return false;}
 auto address=Export<void*>(cef,"cef_urlrequest_create");
 if(!Memory(address,1,true)){Unsupported();return false;}
 void* trampoline=nullptr;
 if(MH_CreateHook(address,reinterpret_cast<void*>(CreateRequest),&trampoline)!=MH_OK){Unsupported();return false;}
 create_hook.original.store(trampoline,std::memory_order_release);
 create_hook.target.store(address,std::memory_order_release);
 // Queueing changes no executable code and never suspends another thread.
 if(MH_QueueEnableHook(address)!=MH_OK){MH_QueueDisableHook(address);Unsupported();return false;}
 root_prepared=true;
 return true;
}
void Finalize(bool applied) noexcept {
 LastError error;
 if(!root_prepared)return;
 root_prepared=false;
 if(applied){create_hook.active.store(true,std::memory_order_release);dirty=true;return;}
 const auto target=create_hook.target.load(std::memory_order_acquire);
 // DisableHook(ERROR_DISABLED) does not clear queued enable intent. Clear it
 // explicitly before disabling a potentially activated prefix of the batch.
 const auto queued=MH_QueueDisableHook(target);
 const auto disabled=MH_DisableHook(target);
 if(disabled==MH_OK||disabled==MH_ERROR_DISABLED||disabled==MH_ERROR_NOT_CREATED)create_hook.active=false;
 if(queued!=MH_OK&&queued!=MH_ERROR_NOT_CREATED)Unsupported();
 // Never remove a trampoline after activation was attempted: a resumed thread
 // can be entering its detour, and secondary callback hooks may already exist.
 Unsupported();
}
void ObserveClient(void* client) noexcept {
 LastError error;
 if(!Enabled())return;
 if(!free_string){Unsupported();return;}
 if(!Prefix(client,sizeof(Base)+18*sizeof(void*))){Unsupported();return;}
 handler_hook.Install(static_cast<Client*>(client)->methods[17],reinterpret_cast<void*>(RequestHandler));
}
void ObserveRenderer(std::wstring_view status) noexcept {
 LastError error;
 // Only fixed producer bits cross the renderer trust boundary; never log input.
 unsigned value=0;
 if(status==L"sender=0 ubi=0 early=0")value=1;
 else if(status==L"sender=1 ubi=0 early=0")value=3;
 else if(status==L"sender=0 ubi=1 early=0")value=5;
 else if(status==L"sender=1 ubi=1 early=0")value=7;
 else return;
 if(renderer.exchange(value)!=value)dirty=true;
}
void DrainStatus() noexcept {
 LastError error;
 if(!dirty.exchange(false))return;
 char line[400];
 const auto producers=renderer.load();
 const bool standalone=create_hook.active.load(),browser=resource_hook.active.load()||fallback_seen.load(),preload=before_hook.active.load();
 // DrainStatus runs on the startup worker. Warn only when capabilities change,
 // never once per blocked request or per unsupported callback observation.
 const unsigned summary=unsigned(Enabled())|(unsigned(standalone)<<1)|(unsigned(browser)<<2)|(unsigned(preload)<<3)|(unsigned(unsupported.load()!=0)<<4)|(producers<<5);
 static unsigned previous=~0u;
 const bool changed=summary!=previous;previous=summary;
 std::snprintf(line,sizeof(line),"privacy status=%s standalone=%u browser=%u preload=%u renderer_seen=%u sender=%u ubi=%u unsupported=%u; early=0 coverage=partial runtime-unverified redirects-and-other-stacks-unverified",
  !Enabled()?"disabled":(standalone||handler_hook.active.load()?"partial":"unsupported"),unsigned(standalone),unsigned(browser),unsigned(preload),unsigned(bool(producers&1)),unsigned(bool(producers&2)),unsigned(bool(producers&4)),unsupported.load());
 try{
  if(changed&&Enabled())history::LogEvent(history::LogKind::Warning,"privacy-partial",
   standalone||handler_hook.active.load()
    ?"Telemetry suppression coverage is partial: CEF callbacks and late renderer producers only; unobserved callbacks, redirects and other network stacks remain unverified."
    :"Telemetry suppression native hooks are unsupported; overall coverage remains partial and unverified.");
  history::QueueDiagnostic(line);
  for(auto rule:{Rule::Gabo,Rule::Logging,Rule::Sentry}){
   const auto count=blocked[static_cast<unsigned>(rule)].load();if(!count)continue;
   std::snprintf(line,sizeof(line),"privacy blocked rule=%s count=%llu",RuleName(rule),count);history::QueueDiagnostic(line);
  }
 }catch(...){}
}
} // namespace privacy
