// Removing cancellation, adding a self-reference transfer to get_url, replacing
// object identity, or forwarding blocked requests must fail these tests.
#include "../native/history_settings.h"
#include "../native/telemetry_filter.cpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "telemetry_decode_fixture.h"
namespace history {
Settings dump_settings;
Settings GetSettings(){return dump_settings;}
std::vector<std::string> telemetry_messages;
int privacy_warnings=0;
int dump_warnings=0;
void LogEvent(LogKind kind,const char* event,const std::string& detail){assert(kind==LogKind::Warning);if(!std::strcmp(event,"privacy-partial")){assert(detail.find("partial")!=std::string::npos);++privacy_warnings;}else{assert(!std::strcmp(event,"privacy-dump")&&detail.find("secret")==std::string::npos);++dump_warnings;}}
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
Storage<Object<7>> post;
Storage<Object<8>> element,file_element;
std::vector<unsigned char> payload={'{','"','a','"',':','[','1',',','2',']','}'};
int body_reads=0,post_reads=0,file_reads=0,element_lists=0;
size_t declared_elements=0;
bool with_file=false,excluded=false,with_unknown=false;
std::wstring content_encoding=L"identity",content_type=L"application/json";
String* GetMethod(Request*){static wchar_t value[]=L"POST";return new String{value,4,nullptr};}
String* GetHeader(Request*,const String* name){
 std::wstring_view key(name->str,name->length);assert(key==L"Content-Type"||key==L"Content-Encoding");
 return key==L"Content-Type"?new String{content_type.data(),content_type.size(),nullptr}:new String{content_encoding.data(),content_encoding.size(),nullptr};
}
Object<7>* GetPost(Request*){++post_reads;post.api.base.add(&post.api.base);return &post.api;}
int Excluded(Object<7>*){return excluded;}
size_t ElementCount(Object<7>*){return declared_elements?declared_elements:(with_file?2:1);}
void Elements(Object<7>*,size_t* count,Object<8>** values){++element_lists;assert(*count>=1&&values[0]==nullptr);values[0]=&element.api;element.api.base.add(&element.api.base);if(with_file){assert(*count>=2&&values[1]==nullptr);values[1]=&file_element.api;file_element.api.base.add(&file_element.api.base);}*count=with_file?2:1;}
int ElementType(Object<8>* self){return self==&file_element.api?(with_unknown?99:2):1;}
String* FileName(Object<8>*){++file_reads;assert(false&&"capture read a referenced file name");return nullptr;}
size_t ByteCount(Object<8>*){return payload.size();}
size_t Bytes(Object<8>*,size_t size,void* out){++body_reads;const auto count=std::min(size,payload.size());std::memcpy(out,payload.data(),count);return count;}
void SetupBody(){
 request.api.methods[3]=reinterpret_cast<void*>(GetMethod);request.api.methods[8]=reinterpret_cast<void*>(GetPost);request.api.methods[12]=reinterpret_cast<void*>(GetHeader);
 post.api.methods[1]=reinterpret_cast<void*>(Excluded);post.api.methods[2]=reinterpret_cast<void*>(ElementCount);post.api.methods[3]=reinterpret_cast<void*>(Elements);
 for(auto* e:{&element.api,&file_element.api}){e->methods[4]=reinterpret_cast<void*>(ElementType);e->methods[5]=reinterpret_cast<void*>(FileName);e->methods[6]=reinterpret_cast<void*>(ByteCount);e->methods[7]=reinterpret_cast<void*>(Bytes);}
}
std::string ReadLog(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}

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
 request.api.methods[1]=reinterpret_cast<void*>(GetUrl);SetupBody();
 client.api.methods[17]=reinterpret_cast<void*>(OriginalHandler);
 handler.api.methods[2]=reinterpret_cast<void*>(OriginalResource);
 resource.api.methods[1]=reinterpret_cast<void*>(OriginalBefore);
 auto address=GetProcAddress(GetModuleHandleW(nullptr),"cef_urlrequest_create");Create create=nullptr;std::memcpy(&create,&address,sizeof(create));
 const bool dump_failure=argc==2&&!std::strcmp(argv[1],"--dump-write-failure");
 const bool dumping=dump_failure||(argc==2&&!std::strcmp(argv[1],"--dump"));
 if(dumping){
  wchar_t temp[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
  const auto folder=std::filesystem::path(temp)/(L"Floggfy-dump-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  assert(std::filesystem::create_directories(folder));
  history::dump_settings.dump_telemetry=true;history::dump_settings.root=folder.wstring();SetupBody();
  assert(privacy::Prepare(GetModuleHandleW(nullptr),true)&&MH_ApplyQueued()==MH_OK);privacy::Finalize(true);
  if(dump_failure){
   const auto log=folder/L"Floggfy-telemetry.log";assert(std::filesystem::create_directory(log));
   auto capture=[&](){Reset();url=L"sp://logging/v3";SetLastError(42);assert(create(&request.api,&url_client.api,&context.api)==nullptr&&GetLastError()==42);privacy::DrainStatus();};
   capture();capture();assert(history::dump_warnings==1&&"persistent write errors must not repeat default warnings");
   assert(std::filesystem::remove(log));
   HANDLE file=CreateFileW(log.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(file!=INVALID_HANDLE_VALUE);
   LARGE_INTEGER end{};end.QuadPart=5*1024*1024;assert(SetFilePointerEx(file,end,nullptr,FILE_BEGIN)&&SetEndOfFile(file));CloseHandle(file);
   capture();capture();assert(history::dump_warnings==2&&std::filesystem::file_size(log)==5*1024*1024);
   bool failed=false,full=false;for(const auto& line:history::telemetry_messages){failed=failed||line.find("write_failed=2")!=line.npos;full=full||line.find("log_full=2")!=line.npos;}
   assert(failed&&full&&post.refs==1&&element.refs==1&&file_reads==0);
   assert(MH_Uninitialize()==MH_OK);std::puts("PASS: write failure and existing 5 MiB cap preserve cancellation and deduplicate warnings");return 0;
  }
  url=L"https://name:secret@spclient.wg.spotify.com/gabo-receiver-service/v3/events?token=secret#secret";SetLastError(42);
  assert(create(&request.api,&url_client.api,&context.api)==nullptr&&GetLastError()==42);
  assert(body_reads==1&&post_reads==1&&post.refs==1&&element.refs==1&&file_reads==0);
  const auto log=folder/L"Floggfy-telemetry.log";assert(!std::filesystem::exists(log)&&"callback performed disk writing");
  privacy::DrainStatus();const auto text=ReadLog(log);
  assert(text.find("POST")!=text.npos&&text.find("https://spclient.wg.spotify.com/gabo-receiver-service/v3/events")!=text.npos);
  assert(text.find("secret")==text.npos&&text.find("name:")==text.npos&&text.find("\"a\": [")!=text.npos);
  assert(text.find("application/json")!=text.npos&&text.find("standalone")!=text.npos);
  assert(text.find("POST https://spclient.wg.spotify.com/gabo-receiver-service/v3/events\n")!=text.npos);
  assert(text.find("Capture: complete")!=text.npos&&text.find("Truncated: no")==text.npos&&text.find("Element 0:")==text.npos);

  // Allowed routes never enter post-data access, even with dumping enabled.
  Reset();url=L"https://spclient.wg.spotify.com/metadata/4/track/1";SetLastError(42);
  assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));assert(post_reads==1&&body_reads==1&&GetLastError()==43);
  auto capture=[&](){Reset();url=L"sp://logging/v3";SetLastError(42);assert(create(&request.api,&url_client.api,&context.api)==nullptr&&GetLastError()==42);};
  // Real production worker decoding must put gzip/protobuf event fields IN this log.
  const auto compressed=telemetry_fixture::Bytes(telemetry_fixture::gzip_protobuf);payload.assign(compressed.begin(),compressed.end());
  content_type=L"application/x-protobuf";content_encoding=L"gzip";capture();
  assert(ReadLog(log).find("ui.page_view")==std::string::npos&&"callback performed worker-only decoding/writing");
  privacy::DrainStatus();const auto decoded_log=ReadLog(log);
  assert(decoded_log.find("ui.page_view")!=std::string::npos&&decoded_log.find("\"unsigned\": \"42\"")!=std::string::npos&&"gzip protobuf event fields must be decoded in the native log");
  assert(decoded_log.find("gzip+protobuf-wire-fields")!=std::string::npos&&decoded_log.find("schema unknown")!=std::string::npos);
  content_type=L"application/json";content_encoding=L"identity";
  // One binary byte stream survives exactly in the full base64 representation.
  payload={0,1,2,0xff,'x'};capture();privacy::DrainStatus();
  assert(ReadLog(log).find("AAEC/3g=")!=std::string::npos);
  // Text that is not one JSON value (Sentry envelopes) stays readable.
  const std::string ndjson="{\"envelope\":1}\n{\"event\":2}\n";payload.assign(ndjson.begin(),ndjson.end());capture();privacy::DrainStatus();
  assert(ReadLog(log).find(ndjson)!=std::string::npos);
  // Content encoding is explicit and never misrepresented as decoded JSON.
  content_encoding=L"gzip";payload={'{','}'};capture();privacy::DrainStatus();content_encoding=L"identity";
  assert(ReadLog(log).find("content encoding not decoded")!=std::string::npos&&ReadLog(log).find("e30=")!=std::string::npos);
  // The body cap, file omission and excluded multipart data remain explicit.
  payload.assign(256*1024+20,'x');with_file=true;excluded=true;capture();privacy::DrainStatus();with_file=false;excluded=false;
  auto limited=ReadLog(log);assert(limited.find("Size: 262,144 B captured")!=limited.npos&&limited.find("body truncated (limit 262,144 B)")!=limited.npos);
  assert(limited.find("1 file element(s) omitted")!=limited.npos&&limited.find("CEF excluded body elements")!=limited.npos&&file_reads==0);
  assert(post.refs==1&&element.refs==1&&file_element.refs==1);
  // Every cancellation entrypoint copies once; the custom cancel callback does not duplicate it.
  privacy::ObserveClient(&client.api);auto h=reinterpret_cast<GetHandler>(client.api.methods[17])(&client.api);
  auto get_resource=reinterpret_cast<GetResource>(h->methods[2]);payload={'{','}'};
  Reset();url=L"sp://logging/v3";int disable=7;SetLastError(42);const auto resource_reads=post_reads;
  auto blocked=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);assert(blocked&&post_reads==resource_reads+1);
  Reset();SetLastError(42);assert(reinterpret_cast<Before>(blocked->methods[1])(blocked,&browser.api,&frame.api,&request.api,&callback.api)==0);
  assert(post_reads==resource_reads+1);Drop(blocked);privacy::DrainStatus();
  Reset();url=L"https://spclient.wg.spotify.com/metadata/4/track/1";disable=7;SetLastError(42);
  auto allowed=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);
  Reset();url=L"sp://logging/v3";SetLastError(42);assert(reinterpret_cast<Before>(allowed->methods[1])(allowed,&browser.api,&frame.api,&request.api,&callback.api)==0);privacy::DrainStatus();
  null_handler=true;h=reinterpret_cast<GetHandler>(client.api.methods[17])(&client.api);get_resource=reinterpret_cast<GetResource>(h->methods[2]);
  Reset();disable=7;SetLastError(42);blocked=get_resource(h,&browser.api,&frame.api,&request.api,1,0,nullptr,&disable);assert(blocked);Drop(blocked);Drop(h);null_handler=false;privacy::DrainStatus();
  const auto hooks=ReadLog(log);assert(hooks.find("Hook: resource\n")!=hooks.npos&&hooks.find("Hook: preload\n")!=hooks.npos&&hooks.find("Hook: resource-default\n")!=hooks.npos);
  // Capability loss records unsupported capture rather than reading invalid callbacks.
  payload={'{','}'};element.api.methods[7]=nullptr;const auto prior_reads=body_reads;capture();privacy::DrainStatus();
  assert(body_reads==prior_reads&&ReadLog(log).find("unsupported fields/elements")!=std::string::npos);
  element.api.methods[7]=reinterpret_cast<void*>(Bytes);
  // A valid JSON prefix plus an unsupported element is not a complete JSON body.
  with_file=true;with_unknown=true;payload={'{','}'};capture();privacy::DrainStatus();with_file=false;with_unknown=false;
  const auto incomplete=ReadLog(log);const auto last_entry=incomplete.substr(incomplete.rfind("=== Blocked request"));
  assert(last_entry.find("Payload (JSON, formatted)")==std::string::npos&&last_entry.find("not validated as a complete JSON value")!=std::string::npos);
  // CEF GetElements builds a full temporary vector; skip it if count exceeds our cap.
  declared_elements=65;const auto lists_before=element_lists;const auto reads_before=body_reads;capture();privacy::DrainStatus();declared_elements=0;
  assert(element_lists==lists_before&&body_reads==reads_before&&post.refs==1);
  assert(ReadLog(log).find("Parts: 0/65 inspected")!=std::string::npos);
  // A full queue drops before reading another body; draining is bounded to two.
  const auto before_queue=body_reads;
  for(unsigned i=0;i<9;++i)capture();
  assert(body_reads==before_queue+8);privacy::DrainStatus();
  assert(ReadLog(log).find("queue_dropped=1")!=std::string::npos);
  for(unsigned i=0;i<4;++i)privacy::DrainStatus();
  // Reach the process capture limit with small valid JSON then verify no more reads.
  for(unsigned i=0;i<24;++i){capture();privacy::DrainStatus();}
  const auto after_limit=body_reads;capture();privacy::DrainStatus();assert(body_reads==after_limit);
  const auto all=ReadLog(log);assert(all.find("captured=32")!=std::string::npos);
  assert(all.find("limit_dropped=1")!=std::string::npos&&post.refs==1&&element.refs==1&&file_element.refs==1);
  for(const auto& line:history::telemetry_messages){assert(line.find("sp://")==std::string::npos&&line.find("https://")==std::string::npos&&line.find("envelope")==std::string::npos);}
  assert(std::distance(std::filesystem::directory_iterator(folder),std::filesystem::directory_iterator{})==1);
  std::printf("PASS: bounded readable dump, JSON/text/base64, refs, sanitized metadata, queue and session limits; log=%s\n",folder.string().c_str());
  assert(MH_Uninitialize()==MH_OK);return 0;
 }
 const bool rollback_before=argc==2&&!std::strcmp(argv[1],"--rollback-before-apply");
 const bool rollback_after=argc==2&&!std::strcmp(argv[1],"--rollback-after-apply");
 if(argc==2&&!rollback_before&&!rollback_after){
  const bool disabled=!std::strcmp(argv[1],"--disabled");
  assert(disabled||!std::strcmp(argv[1],"--missing-exports"));
  history::dump_settings.dump_telemetry=disabled;
  SetLastError(42);assert(!privacy::Prepare(disabled?GetModuleHandleW(nullptr):GetModuleHandleW(L"kernel32.dll"),!disabled));
  privacy::ObserveClient(nullptr);assert(GetLastError()==42&&privacy::Enabled()!=disabled);
  url=L"sp://logging/v3";assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
  assert(forwarded==1&&freed==0&&body_reads==0&&post_reads==0&&GetLastError()==43);privacy::DrainStatus();
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
 // Each category can restore forwarding without weakening other categories.
 struct CategoryCase {privacy::Rule rule;bool history::Settings::*field;const wchar_t* url;};
 const CategoryCase categories[]={
  {privacy::Rule::Gabo,&history::Settings::block_event_reports,L"https://spclient.wg.spotify.com/gabo-receiver-service/v3/events"},
  {privacy::Rule::Logging,&history::Settings::block_internal_logging,L"sp://logging/v2/send_pending_message"},
  {privacy::Rule::Sentry,&history::Settings::block_sentry,L"https://o22381.ingest.sentry.io/api/114855/envelope/"},
  {privacy::Rule::Playback,&history::Settings::block_playback_reports,L"https://spclient.wg.spotify.com/melody/v1/msg/batch"},
  {privacy::Rule::Growth,&history::Settings::block_growth_analytics,L"https://www.spotify.com/api/growth-events/wwwanalytics"},
  {privacy::Rule::Crash,&history::Settings::block_crash_reports,L"https://crashdump.spotify.com/"},
  {privacy::Rule::TrackError,&history::Settings::block_track_errors,L"https://spclient.wg.spotify.com/track-error/v1/errors"},
  {privacy::Rule::AITelemetry,&history::Settings::block_ai_telemetry,L"https://spclient.wg.spotify.com/v1/telemetry/sandbox"}
 };
 for(const auto& item:categories){
  history::Settings options;options.*(item.field)=false;privacy::ConfigureCategories(options);
  Reset();url=item.url;SetLastError(42);const auto previous=forwarded;
  assert(create(&request.api,&url_client.api,&context.api)==reinterpret_cast<void*>(0x1234));
  assert(forwarded==previous+1&&GetLastError()==43);
  options.*(item.field)=true;privacy::ConfigureCategories(options);
  Reset();SetLastError(42);
  assert(create(&request.api,&url_client.api,&context.api)==nullptr&&forwarded==previous+1&&GetLastError()==42);
 }
 privacy::ConfigureCategories(history::Settings{});
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
 privacy::ObserveRendererCapabilities(L"fetch=1 xhr=1 beacon=1 cosmos=1 early=0");
 privacy::ObserveRendererCount(L"playback_reports=1");privacy::DrainStatus();
 assert(privacy::renderer_capabilities.load()==31&&privacy::renderer_blocked[static_cast<unsigned>(privacy::Rule::Playback)].load()==1);
 privacy::ObserveRendererCount(L"playback_reports=999999999999999999999");
 privacy::ObserveRendererCount(L"playback_reports=1 https://secret.invalid");
 privacy::ObserveRendererCapabilities(L"fetch=1 xhr=1 beacon=1 cosmos=1 early=1");
 privacy::ObserveRendererCapabilities(L"fetch=1 xhr=1 beacon=1 cosmos=1 early=0 extra");
 assert(privacy::renderer_capabilities.load()==31&&privacy::renderer_blocked[static_cast<unsigned>(privacy::Rule::Playback)].load()==1);
 history::Settings category_off;category_off.block_playback_reports=false;privacy::ConfigureCategories(category_off);
 privacy::ObserveRendererCount(L"playback_reports=1");
 assert(privacy::renderer_blocked[static_cast<unsigned>(privacy::Rule::Playback)].load()==1);
 privacy::ConfigureCategories(history::Settings{});
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
 assert(body_reads==0&&post_reads==0&&"dumping off must never inspect body data");
 assert(MH_Uninitialize()==MH_OK);
 std::puts("PASS: native cancellation, allowed forwarding, identity, transferred refs, LastError and partial diagnostics");
}
