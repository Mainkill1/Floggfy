// Removing cancellation, adding a self-reference transfer to get_url, replacing
// object identity, or forwarding blocked requests must fail these tests.
#include "../native/telemetry_filter.cpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
namespace history {
std::vector<std::string> telemetry_messages;
int privacy_warnings=0;
void LogEvent(LogKind kind,const char*,const std::string& detail){assert(kind==LogKind::Warning&&detail.find("partial")!=std::string::npos);++privacy_warnings;}
void QueueDiagnostic(const char* text){telemetry_messages.emplace_back(text);}
}
namespace fixture {
struct Base {size_t size;void(*add)(Base*);int(*release)(Base*);int(*one)(Base*);int(*any)(Base*);};
struct String {wchar_t* str;size_t length;void(*dtor)(wchar_t*);};
template<size_t N> struct Object {Base base;void* methods[N];};
using Request=Object<23>;using Client=Object<19>;using Handler=Object<11>;using Resource=Object<8>;
template<class T> struct Storage {
 uintptr_t wrapper_identity=0x12345678;
 T api{};int refs=1,drops=0,adds=0;
 static Storage* Get(Base* b){return reinterpret_cast<Storage*>(reinterpret_cast<char*>(b)-offsetof(Storage,api));}
 static void Add(Base* b){++Get(b)->refs;++Get(b)->adds;}
 static int Drop(Base* b){++Get(b)->drops;return --Get(b)->refs==0;}
 static int One(Base* b){return Get(b)->refs==1;}
 static int Any(Base* b){return Get(b)->refs>0;}
 Storage(){api.base={sizeof(T),Add,Drop,One,Any};}
 void Reset(){refs=1;drops=adds=0;}
};
Storage<Request> request;
Storage<Object<0>> browser,frame,callback,context,url_client;
Storage<Client> client;
Storage<Handler> handler;
Storage<Resource> resource;
std::wstring url;
int forwarded=0,freed=0,resource_calls=0,before_calls=0;
bool null_handler=false,null_resource=false;
String* GetUrl(Request* self){assert(self==&request.api);SetLastError(123);return new String{url.data(),url.size(),nullptr};}
void Drop(void* p){if(p)static_cast<Base*>(p)->release(static_cast<Base*>(p));}
using Create=void*(*)(Request*,void*,void*);
using GetHandler=Handler*(*)(Client*);
using GetResource=Resource*(*)(Handler*,void*,void*,Request*,int,int,const String*,int*);
using Before=int(*)(Resource*,void*,void*,Request*,void*);
__attribute__((noinline)) int OriginalBefore(Resource* self,void* b,void* f,Request* r,void* cb){
 if(!self||!r||!cb){++before_calls;SetLastError(43);return 1;}
 assert(self==&resource.api&&b==&browser.api&&f==&frame.api&&r==&request.api&&cb==&callback.api);
 assert(GetLastError()==42);++before_calls;Drop(b);Drop(f);Drop(r);Drop(cb);SetLastError(43);return 2;
}
__attribute__((noinline)) Resource* OriginalResource(Handler* self,void* b,void* f,Request* r,int nav,int dl,const String* initiator,int* disable){
 if(!self||!b||!f||!r||!disable){++resource_calls;SetLastError(43);return nullptr;}
 assert(self==&handler.api&&b==&browser.api&&f==&frame.api&&r==&request.api);
 assert(nav==1&&dl==0&&initiator==nullptr&&disable&&*disable==7&&GetLastError()==42);
 ++resource_calls;Drop(b);Drop(f);Drop(r);*disable=9;SetLastError(43);return null_resource?nullptr:&resource.api;
}
__attribute__((noinline)) Handler* OriginalHandler(Client* self){assert(self==&client.api);SetLastError(43);return null_handler?nullptr:&handler.api;}
void Reset(){request.Reset();browser.Reset();frame.Reset();callback.Reset();context.Reset();url_client.Reset();}
}
extern "C" __declspec(dllexport) void cef_string_userfree_utf16_free(fixture::String* s){++fixture::freed;delete s;SetLastError(124);}
extern "C" __declspec(dllexport) __attribute__((noinline)) void* cef_urlrequest_create(fixture::Request* r,void* c,void* context){
 if(!r||!c){++fixture::forwarded;assert(GetLastError()==42);SetLastError(43);return nullptr;}
 assert(r==&fixture::request.api&&c==&fixture::url_client.api&&context==&fixture::context.api);
 assert(GetLastError()==42);++fixture::forwarded;fixture::Drop(r);fixture::Drop(c);fixture::Drop(context);SetLastError(43);return reinterpret_cast<void*>(0x1234);
}
int main(int argc,char** argv){
 using namespace fixture;
 request.api.methods[1]=reinterpret_cast<void*>(GetUrl);
 client.api.methods[17]=reinterpret_cast<void*>(OriginalHandler);
 handler.api.methods[2]=reinterpret_cast<void*>(OriginalResource);
 resource.api.methods[1]=reinterpret_cast<void*>(OriginalBefore);
 auto address=GetProcAddress(GetModuleHandleW(nullptr),"cef_urlrequest_create");Create create=nullptr;std::memcpy(&create,&address,sizeof(create));
 const bool rollback_before=argc==2&&!std::strcmp(argv[1],"--rollback-before-apply");
 const bool rollback_after=argc==2&&!std::strcmp(argv[1],"--rollback-after-apply");
 if(argc==2&&!rollback_before&&!rollback_after){
  const bool disabled=!std::strcmp(argv[1],"--disabled");
  assert(disabled||!std::strcmp(argv[1],"--missing-exports"));
  SetLastError(42);assert(!privacy::Prepare(disabled?GetModuleHandleW(nullptr):GetModuleHandleW(L"kernel32.dll"),!disabled));
  privacy::ObserveClient(nullptr);assert(GetLastError()==42&&privacy::Enabled()!=disabled);
  url=L"sp://logging/v3";assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
  assert(forwarded==1&&freed==0&&GetLastError()==43);privacy::DrainStatus();
  assert(!history::telemetry_messages.empty());
  std::puts("PASS: disabled or unavailable filter preserves original request behavior");return 0;
 }
 assert(privacy::Prepare(GetModuleHandleW(nullptr),true));
 url=L"sp://logging/v3";SetLastError(42);
 assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234)&&"preparation activated the factory before the shared batch");
 assert(forwarded==1&&freed==0&&GetLastError()==43);Reset();forwarded=0;
 if(rollback_before||rollback_after){
  if(rollback_after){
   assert(MH_ApplyQueued()==MH_OK);
   // A partially applied batch may resume a browser callback before failing.
   privacy::ObserveClient(&client.api);
   assert(reinterpret_cast<GetHandler>(client.api.methods[17])(&client.api)==&handler.api);
  }
  SetLastError(42);privacy::Finalize(false);assert(GetLastError()==42);
  // Applying other queued owners later must not accidentally enable this root.
  assert(MH_ApplyQueued()==MH_OK);SetLastError(42);
  assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
  assert(forwarded==1&&request.drops==1&&url_client.drops==1&&context.drops==1&&GetLastError()==43);
  void* ignored=nullptr;
  assert(MH_CreateHook(reinterpret_cast<void*>(address),reinterpret_cast<void*>(create),&ignored)==MH_ERROR_ALREADY_CREATED&&"rollback removed a potentially reachable trampoline");
  if(rollback_after){
   Reset();int disable=7;SetLastError(42);
   const auto get_resource=reinterpret_cast<GetResource>(handler.api.methods[2]);
   auto blocked=get_resource(&handler.api,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);
   assert(blocked&&resource_calls==0&&request.drops==1&&GetLastError()==42);Drop(blocked);
  }
  assert(MH_Uninitialize()==MH_OK);
  std::puts("PASS: rollback clears queued root activation and retains reachable callback/trampoline state");return 0;
 }
 assert(MH_ApplyQueued()==MH_OK);privacy::Finalize(true);
 url=L"https://spclient.wg.spotify.com/gabo-receiver-service/v3/events";
 SetLastError(42);
 assert(create(&request.api,&url_client.api,&context.api)==nullptr&&"blocked standalone request reached original factory");
 assert(forwarded==0&&request.drops==1&&request.adds==0&&url_client.drops==1&&context.drops==1&&freed==1&&GetLastError()==42);

 for(const auto allowed:{L"https://spclient.wg.spotify.com/metadata/4/track/1",L"https://spclient.wg.spotify.com/color-lyrics/v2/track/1",L"https://accounts.spotify.com/api/login"}){
  Reset();url=allowed;SetLastError(42);const auto previous=forwarded;
  assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
  assert(forwarded==previous+1&&request.drops==1&&request.adds==0&&url_client.drops==1&&context.drops==1&&GetLastError()==43);
 }
 const auto original_client=client.api;const auto original_handler=handler.api; // Identity remains public and private.
 SetLastError(42);privacy::ObserveClient(&client.api);assert(GetLastError()==42);
 auto get_handler=reinterpret_cast<GetHandler>(client.api.methods[17]);
 auto h=get_handler(&client.api);assert(h==&handler.api&&GetLastError()==43);
 auto get_resource=reinterpret_cast<GetResource>(h->methods[2]);
 Reset();url=L"sp://logging/v3/events";int disable=7;SetLastError(42);
 auto blocked=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);
 assert(blocked&&blocked!=&resource.api&&resource_calls==0&&request.drops==1&&browser.drops==1&&frame.drops==1&&GetLastError()==42);
 Reset();SetLastError(42);
 assert(reinterpret_cast<Before>(blocked->methods[1])(blocked,&browser.api,&frame.api,&request.api,&callback.api)==0);
 assert(request.drops==1&&browser.drops==1&&frame.drops==1&&callback.drops==1&&GetLastError()==42);Drop(blocked);
 Reset();url=L"https://spclient.wg.spotify.com/metadata/4/track/1";disable=7;SetLastError(42);
 auto allowed=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);
 assert(allowed==&resource.api&&resource_calls==1&&disable==9&&GetLastError()==43);
 Reset();SetLastError(42);
 assert(reinterpret_cast<Before>(allowed->methods[1])(allowed,&browser.api,&frame.api,&request.api,&callback.api)==2);
 assert(before_calls==1&&request.drops==1&&callback.drops==1&&GetLastError()==43);
 // A resource originally allowed can target telemetry by the pre-load callback.
 Reset();url=L"https://spclient.wg.spotify.com/gabo-receiver-service/v3/events";SetLastError(42);
 assert(reinterpret_cast<Before>(allowed->methods[1])(allowed,&browser.api,&frame.api,&request.api,&callback.api)==0);
 assert(before_calls==1&&request.drops==1&&callback.drops==1&&GetLastError()==42);
 assert(!std::memcmp(&original_client,&client.api,sizeof(original_client))&&!std::memcmp(&original_handler,&handler.api,sizeof(original_handler)));
 assert(client.wrapper_identity==0x12345678&&handler.wrapper_identity==0x12345678);
 // Required null callback arguments follow the original early-return contract.
 Reset();url=L"sp://logging/v3";SetLastError(42);
 assert(reinterpret_cast<Before>(allowed->methods[1])(allowed,&browser.api,&frame.api,&request.api,nullptr)==1);
 assert(before_calls==2&&request.drops==0&&browser.drops==0&&GetLastError()==43);
 Reset();SetLastError(42);
 assert(get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,nullptr)==nullptr);
 assert(resource_calls==2&&request.drops==0&&browser.drops==0&&GetLastError()==43);
 // No original request handler: a valid owned handler can still cancel requests.
 null_handler=true;h=get_handler(&client.api);assert(h&&h!=&handler.api);
 get_resource=reinterpret_cast<GetResource>(h->methods[2]);
 Reset();url=L"sp://logging/v3";disable=7;SetLastError(42);
 blocked=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);assert(blocked&&GetLastError()==42);Drop(blocked);
 Reset();url=L"https://spclient.wg.spotify.com/color-lyrics/v2/track/1";disable=7;SetLastError(42);
 assert(get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable)==nullptr);
 assert(request.drops==1&&browser.drops==1&&frame.drops==1&&disable==7&&GetLastError()==42);Drop(h);
 // Unsupported prefix and inaccessible objects must be ignored safely.
 SetLastError(42);privacy::ObserveClient(reinterpret_cast<void*>(1));
 Client short_client{};short_client.base.size=sizeof(Base);privacy::ObserveClient(&short_client);assert(GetLastError()==42);
 // Renderer diagnostics accept only the four fixed combinations.
 privacy::ObserveRenderer(L"sender=1 ubi=1 early=0");privacy::DrainStatus();
 const auto diagnostics=history::telemetry_messages.size();
 assert(history::privacy_warnings>0&&"enabled partial coverage must be visible at default log level");
 privacy::ObserveRenderer(L"sender=1 ubi=1 early=1");
 privacy::ObserveRenderer(L"sender=1 ubi=1 early=0 https://secret.invalid/token");
 privacy::DrainStatus();assert(history::telemetry_messages.size()==diagnostics);
 assert(!history::telemetry_messages.empty());
 for(const auto& line:history::telemetry_messages){assert(line.find("https:")==std::string::npos&&line.find("sp://")==std::string::npos);}
 // CEF rejects required null arguments before consuming other references.
 Reset();url=L"sp://logging/v3";SetLastError(42);const auto calls_before_null=forwarded;
 assert(create(&request.api,nullptr,&context.api)==nullptr);
 assert(forwarded==calls_before_null+1&&request.drops==0&&context.drops==0&&GetLastError()==43);
 // Missing get_url capability must preserve the original call and its refs.
 Reset();request.api.methods[1]=nullptr;SetLastError(42);const auto previous=forwarded;
 assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
 assert(forwarded==previous+1&&request.drops==1&&GetLastError()==43);
 assert(MH_Uninitialize()==MH_OK);
 std::puts("PASS: native cancellation, allowed forwarding, identity, transferred refs, LastError and partial diagnostics");
}
