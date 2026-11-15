#define WIN32_LEAN_AND_MEAN
#include "telemetry_filter.h"
#include "telemetry_policy.h"
#include "async_log.h"
#include "history_settings.h"
#include "vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <array>
#include <memory>
#include <filesystem>
#include <algorithm>

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
// Dumping is an explicit, restart-only diagnostic. Callbacks only copy bounded
// in-memory data; the existing startup worker drains the queue to one local log.
using Post=Object<7>;using Element=Object<8>;
constexpr size_t kDumpBytes=256*1024,kDumpQueue=8,kDumpElements=64,kDumpLogBytes=5*1024*1024;
struct DumpElement {int type=-1;size_t reported=0,offset=0,copied=0;};
struct DumpRecord {
 unsigned id=0;SYSTEMTIME utc{};Rule rule=Rule::Allow;const char* hook="unknown";
 std::wstring method,url,content_type,encoding;
 bool truncated=false,metadata_truncated=false,excluded=false,unsupported=false,has_post=false;
 size_t length=0,declared_elements=0,elements=0,file_elements=0;
 std::array<DumpElement,kDumpElements> parts{};
 std::array<unsigned char,kDumpBytes> bytes{};
};
struct DumpState {
 bool enabled=false;std::wstring location;SYSTEMTIME started{};
 SRWLOCK lock=SRWLOCK_INIT;
 std::array<std::unique_ptr<DumpRecord>,kDumpQueue> queue{};size_t head=0,count=0;
 std::atomic<unsigned> captured{0},queue_drops{0},limit_drops{0},busy_drops{0},allocation_drops{0};
 std::atomic<unsigned> truncated{0},unsupported{0},files{0},excluded{0},revision{0};
 unsigned written=0,write_failed=0,log_full=0,last_summary=0,last_failure=0;
};
DumpState dump;
template<size_t N> bool Method(Object<N>* object,size_t index) noexcept {
 return index<N&&Prefix(object,sizeof(Base)+(index+1)*sizeof(void*))&&Memory(object->methods[index],1,true);
}
struct RefDrop {void* value;~RefDrop(){if(value&&Releasable(value))Drop(value);}};
struct StringDrop {String* value;~StringDrop(){if(value)free_string(value);}};
std::wstring CopyString(String* value,size_t limit,DumpRecord& record){
 StringDrop release{value};if(!value)return {};
 if(!Memory(value,sizeof(String))||value->length>32768||(!value->str&&value->length)||
    (value->length&&!Memory(value->str,value->length*sizeof(wchar_t)))){record.unsupported=true;return {};}
 const auto count=(std::min)(value->length,limit);if(count<value->length)record.metadata_truncated=true;
 return count?std::wstring(value->str,count):std::wstring{};
}
std::wstring DumpField(Request* request,size_t index,size_t limit,DumpRecord& record){
 if(!Method(request,index)){record.unsupported=true;return {};}
 return CopyString(reinterpret_cast<String*(*)(Request*)>(request->methods[index])(request),limit,record);
}
std::wstring DumpHeader(Request* request,const wchar_t* name,DumpRecord& record){
 if(!Method(request,12)){record.unsupported=true;return {};}
 String key{const_cast<wchar_t*>(name),wcslen(name),nullptr};
 return CopyString(reinterpret_cast<String*(*)(Request*,const String*)>(request->methods[12])(request,&key),1024,record);
}
std::wstring SanitizedDestination(std::wstring url){
 const auto suffix=url.find_first_of(L"?#");if(suffix!=url.npos)url.resize(suffix);
 const auto scheme=url.find(L"://");if(scheme==url.npos)return L"unavailable";
 const auto start=scheme+3,end=url.find_first_of(L"/\\",start);
 const auto at=url.rfind(L'@',end==url.npos?url.size():end);
 if(at!=url.npos&&at>=start)url.erase(start,at-start+1);
 return url;
}
void CopyBody(Request* request,DumpRecord& record){
 if(!Method(request,8)){record.unsupported=true;return;}
 auto post=reinterpret_cast<Post*(*)(Request*)>(request->methods[8])(request);
 if(!post)return;
 record.has_post=true;RefDrop release_post{post};
 if(!Releasable(post)||!Method(post,1)||!Method(post,2)||!Method(post,3)){record.unsupported=true;return;}
 record.excluded=reinterpret_cast<int(*)(Post*)>(post->methods[1])(post)!=0;
 record.declared_elements=reinterpret_cast<size_t(*)(Post*)>(post->methods[2])(post);
 // CEF's generated GetElements materializes the complete vector internally;
 // limiting only the output array would not bound that temporary allocation.
 if(record.declared_elements>kDumpElements){record.truncated=true;record.unsupported=true;return;}
 std::array<Element*,kDumpElements> elements{};
 struct ReleaseElements {std::array<Element*,kDumpElements>& values;~ReleaseElements(){for(auto value:values)if(value&&Releasable(value))Drop(value);}} release{elements};
 auto count=(std::min)(record.declared_elements,elements.size());
 // CEF GetElements consumes initial array references (all null here) and
 // transfers one reference for each returned element. self stays borrowed.
 if(count)reinterpret_cast<void(*)(Post*,size_t*,Element**)>(post->methods[3])(post,&count,elements.data());
 if(count>elements.size()){record.unsupported=true;count=elements.size();}
 record.elements=count;if(count<record.declared_elements)record.truncated=true;
 for(size_t i=0;i<count;++i){
  auto element=elements[i];auto& part=record.parts[i];part.offset=record.length;
  if(!element||!Releasable(element)||!Method(element,4)){record.unsupported=true;continue;}
  part.type=reinterpret_cast<int(*)(Element*)>(element->methods[4])(element);
  if(part.type==2){++record.file_elements;continue;} // Never call get_file or read a file.
  if(part.type==0)continue;
  if(part.type!=1||!Method(element,6)||!Method(element,7)){record.unsupported=true;continue;}
  part.reported=reinterpret_cast<size_t(*)(Element*)>(element->methods[6])(element);
  const auto capacity=(std::min)(part.reported,kDumpBytes-record.length);
  if(capacity){
   part.copied=reinterpret_cast<size_t(*)(Element*,size_t,void*)>(element->methods[7])(element,capacity,record.bytes.data()+record.length);
   if(part.copied>capacity){part.copied=capacity;record.unsupported=true;}
   record.length+=part.copied;
  }
  if(part.copied<part.reported)record.truncated=true;
 }
}
void Capture(Request* request,Rule rule,const char* hook) noexcept {
 if(!dump.enabled)return;
 if(!TryAcquireSRWLockExclusive(&dump.lock)){++dump.busy_drops;++dump.revision;return;}
 struct Unlock {~Unlock(){ReleaseSRWLockExclusive(&dump.lock);}} unlock;
 if(dump.captured.load()>=32){++dump.limit_drops;++dump.revision;return;}
 if(dump.count==kDumpQueue){++dump.queue_drops;++dump.revision;return;}
 try{
  auto record=std::make_unique<DumpRecord>();record->id=dump.captured.load()+1;record->rule=rule;record->hook=hook;GetSystemTime(&record->utc);
  record->method=DumpField(request,3,64,*record);
  record->url=SanitizedDestination(DumpField(request,1,4096,*record));
  record->content_type=DumpHeader(request,L"Content-Type",*record);
  record->encoding=DumpHeader(request,L"Content-Encoding",*record);
  CopyBody(request,*record);
  if(record->truncated)++dump.truncated;
  if(record->unsupported)++dump.unsupported;
  if(record->file_elements)++dump.files;
  if(record->excluded)++dump.excluded;
  dump.queue[(dump.head+dump.count)%kDumpQueue]=std::move(record);++dump.count;++dump.captured;
 }catch(...){++dump.allocation_drops;}
 ++dump.revision;
}
std::string DumpUtf8(std::wstring_view value){
 if(value.empty())return {};
 const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
 if(!size)return "[invalid Unicode]";
 std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),size,nullptr,nullptr);return out;
}
std::string Inline(std::wstring_view value){
 std::string out;for(const unsigned char c:DumpUtf8(value)){
  if(c<32||c==127){char escaped[5];std::snprintf(escaped,sizeof(escaped),"\\x%02x",c);out+=escaped;}else out+=char(c);
 }return out;
}
std::string Stamp(const SYSTEMTIME& time){
 char out[40];std::snprintf(out,sizeof(out),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",unsigned(time.wYear),unsigned(time.wMonth),unsigned(time.wDay),unsigned(time.wHour),unsigned(time.wMinute),unsigned(time.wSecond),unsigned(time.wMilliseconds));return out;
}
// Small worker-only JSON validator/formatter. It copies token spelling rather
// than decoding strings/numbers; no schema, allocations per node or dependency.
class PrettyJson {
 std::string_view input;size_t position=0;std::string output;
 void Space(){while(position<input.size()&&(input[position]==' '||input[position]=='\r'||input[position]=='\n'||input[position]=='\t'))++position;}
 bool Add(std::string_view text){if(output.size()+text.size()>512*1024)return false;output+=text;return true;}
 bool Newline(unsigned depth){return Add("\n")&&Add(std::string(depth*2,' '));}
 bool StringToken(){
  const auto start=position++;bool ended=false;
  while(position<input.size()){
   unsigned char c=input[position++];if(c=='"'){ended=true;break;}if(c<32)return false;
   if(c=='\\'){
    if(position==input.size())return false;
    c=input[position++];
    if(c=='u'){for(unsigned n=0;n<4;++n){if(position==input.size()||detail::Hex(wchar_t(input[position++]))<0)return false;}}
    else if(c!='"'&&c!='\\'&&c!='/'&&c!='b'&&c!='f'&&c!='n'&&c!='r'&&c!='t')return false;
   }
  }
  return ended&&Add(input.substr(start,position-start));
 }
 bool Value(unsigned depth){
  if(depth>32)return false;
  Space();if(position==input.size())return false;const char c=input[position];
  if(c=='"')return StringToken();
  if(c=='{'||c=='['){
   const bool object=c=='{';const char close=object?'}':']';++position;if(!Add({&c,1}))return false;
   Space();if(position<input.size()&&input[position]==close){++position;return Add({&close,1});}
   for(;;){
    if(!Newline(depth+1))return false;
    Space();if(object){if(position==input.size()||input[position]!='"'||!StringToken())return false;Space();if(position==input.size()||input[position++]!=':'||!Add(": "))return false;}
    if(!Value(depth+1))return false;
    Space();if(position==input.size())return false;
    const auto next=input[position++];if(next==close)return Newline(depth)&&Add({&close,1});
    if(next!=','||!Add(","))return false;
   }
  }
  for(const auto token:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(input.substr(position,token.size())==token){position+=token.size();return Add(token);}
  const auto start=position;if(input[position]=='-')++position;
  if(position==input.size())return false;
  if(input[position]=='0')++position;
  else {const auto first=position;while(position<input.size()&&input[position]>='0'&&input[position]<='9')++position;if(first==position)return false;}
  if(position<input.size()&&input[position]=='.'){++position;const auto first=position;while(position<input.size()&&input[position]>='0'&&input[position]<='9')++position;if(first==position)return false;}
  if(position<input.size()&&(input[position]=='e'||input[position]=='E')){++position;if(position<input.size()&&(input[position]=='+'||input[position]=='-'))++position;const auto first=position;while(position<input.size()&&input[position]>='0'&&input[position]<='9')++position;if(first==position)return false;}
  return Add(input.substr(start,position-start));
 }
public:
 explicit PrettyJson(std::string_view text):input(text){}
 std::string Run(){if(!Value(0))return {};Space();return position==input.size()?std::move(output):std::string{};}
};
std::string Base64(const unsigned char* bytes,size_t size){
 static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 std::string output;output.reserve((size+2)/3*4+size/48);
 for(size_t i=0;i<size;i+=3){
  const unsigned value=(unsigned(bytes[i])<<16)|(i+1<size?unsigned(bytes[i+1])<<8:0)|(i+2<size?bytes[i+2]:0);
  output+=alphabet[(value>>18)&63];output+=alphabet[(value>>12)&63];output+=i+1<size?alphabet[(value>>6)&63]:'=';output+=i+2<size?alphabet[value&63]:'=';
  if((i/3+1)%16==0)output+='\n';
 }return output;
}
std::string FormatDump(const DumpRecord& record){
 std::string out="\n=== Blocked request "+std::to_string(record.id)+" | "+Stamp(record.utc)+" | pid="+std::to_string(GetCurrentProcessId())+" ===\n";
 out+="Session: "+Stamp(dump.started)+"\nMethod: "+Inline(record.method)+"\nDestination: "+Inline(record.url)+"\nRule: "+RuleName(record.rule)+"\nHook: "+record.hook;
 out+="\nContent-Type: "+Inline(record.content_type)+"\nContent-Encoding: "+Inline(record.encoding);
 out+="\nCaptured bytes: "+std::to_string(record.length)+" / limit 262144\nPost data: "+(record.has_post?std::string("yes"):std::string("none"));
 out+="\nTruncated: "+std::string(record.truncated?"yes":"no")+"\nMetadata truncated: "+(record.metadata_truncated?"yes":"no")+"\nExcluded elements: "+(record.excluded?"yes":"no")+"\nUnsupported fields/elements: "+(record.unsupported?"yes":"no");
 out+="\nFile elements omitted: "+std::to_string(record.file_elements)+"\nElements copied/declared: "+std::to_string(record.elements)+"/"+std::to_string(record.declared_elements)+"\n";
 for(size_t i=0;i<record.elements;++i){const auto& part=record.parts[i];out+="Element "+std::to_string(i)+": type="+(part.type==1?"bytes":part.type==2?"file (not read)":part.type==0?"empty":"unsupported")+std::string(" offset=")+std::to_string(part.offset)+" reported="+std::to_string(part.reported)+" copied="+std::to_string(part.copied)+"\n";}
 std::string_view bytes(reinterpret_cast<const char*>(record.bytes.data()),record.length);
 const bool identity=record.encoding.empty()||detail::Equal(record.encoding,L"identity");
 bool text=identity&&!bytes.empty()&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0)>0;
 for(unsigned char c:bytes)if((c<32&&c!='\n'&&c!='\r'&&c!='\t')||c==127){text=false;break;}
 auto pretty=text&&!record.truncated&&!record.excluded&&!record.file_elements&&!record.unsupported?PrettyJson(bytes).Run():std::string{};
 if(!pretty.empty())out+="Payload (JSON, formatted):\n"+pretty;
 else if(text)out+="Payload (UTF-8 text; not validated as a complete JSON value):\n"+std::string(bytes);
 else out+=std::string("Payload (base64; ")+(identity?"binary/empty":"content encoding not decoded")+"):\n"+Base64(record.bytes.data(),record.length);
 return out+"\n=== End request "+std::to_string(record.id)+" ===\n";
}
// Open without write sharing so size checking and appending are one transaction
// across processes. Existing logs are retained; a full log stops accepting data.
bool AppendDump(const std::string& text,bool& full){
 std::error_code error;std::filesystem::create_directories(dump.location,error);if(error)return false;
 const auto path=std::filesystem::path(dump.location)/L"Floggfy-telemetry.log";
 HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size{};bool success=GetFileSizeEx(file,&size)!=0;
 if(success&&(size.QuadPart<0||static_cast<unsigned long long>(size.QuadPart)>kDumpLogBytes||text.size()>kDumpLogBytes-static_cast<size_t>(size.QuadPart))){full=true;success=false;}
 if(success){
  success=SetFilePointerEx(file,size,nullptr,FILE_BEGIN)!=0;DWORD written=0;
  if(success){success=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size();
   if(!success){SetFilePointerEx(file,size,nullptr,FILE_BEGIN);SetEndOfFile(file);}}
 }
 CloseHandle(file);return success;
}
void DrainDumps() noexcept {
 if(!dump.enabled)return;
 try{
  unsigned failure=0;
  for(unsigned batch=0;batch<2;++batch){
   std::unique_ptr<DumpRecord> record;
   if(!TryAcquireSRWLockExclusive(&dump.lock))break;
   if(dump.count){record=std::move(dump.queue[dump.head]);dump.head=(dump.head+1)%kDumpQueue;--dump.count;}
   ReleaseSRWLockExclusive(&dump.lock);if(!record)break;
   bool full=false;
   try{if(AppendDump(FormatDump(*record),full))++dump.written;else if(full){++dump.log_full;failure=2;}else{++dump.write_failed;failure=1;}}catch(...){++dump.write_failed;failure=1;}
   ++dump.revision;
  }
  const auto revision=dump.revision.load();if(revision==dump.last_summary)return;dump.last_summary=revision;
  char summary[600];std::snprintf(summary,sizeof(summary),"Telemetry dump summary: captured=%u written=%u write_failed=%u log_full=%u queue_dropped=%u limit_dropped=%u busy_dropped=%u allocation_dropped=%u truncated=%u unsupported=%u file_requests=%u excluded_requests=%u\n",dump.captured.load(),dump.written,dump.write_failed,dump.log_full,dump.queue_drops.load(),dump.limit_drops.load(),dump.busy_drops.load(),dump.allocation_drops.load(),dump.truncated.load(),dump.unsupported.load(),dump.files.load(),dump.excluded.load());
  bool full=false;if(!AppendDump(summary,full))failure=full?2:1;
  if(failure&&failure!=dump.last_failure)history::LogEvent(history::LogKind::Warning,"privacy-dump",failure==2?"Telemetry dump log is full; capture/write/drop counters are available in diagnostic logging.":"Telemetry dump write failed; capture/write/drop counters are available in diagnostic logging.");
  dump.last_failure=failure;
  history::QueueDiagnostic(summary);
 }catch(...){++dump.write_failed;}
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
  if(rule!=Rule::Allow){Capture(request,rule,"preload");Count(rule);return Cancel(self,browser,frame,request,callback);}
 }
 error.Restore();const auto result=before_hook.Original<Before>()(self,browser,frame,request,callback);error.value=GetLastError();return result;
}
Resource* ResourceRequest(Handler* self,void* browser,void* frame,Request* request,int navigation,int download,const String* initiator,int* disable);
Resource* DefaultResource(Handler*,void* browser,void* frame,Request* request,int,int,const String*,int*){
 LastError error;
 Resource* result=nullptr;
 if(Enabled()){
  const auto rule=RequestRule(request);
  if(rule!=Rule::Allow){result=Cancellation();if(result){Capture(request,rule,"resource-default");Count(rule);}}
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
  if(rule!=Rule::Allow){if(auto cancel=Cancellation()){Capture(request,rule,"resource");Count(rule);Drop(browser);Drop(frame);Drop(request);return cancel;}}
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
  if(rule!=Rule::Allow){Capture(request,rule,"standalone");Count(rule);Drop(request);Drop(client);Drop(context);return nullptr;}
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
 try{const auto settings=history::GetSettings();if(settings.dump_telemetry&&!settings.root.empty()){dump.location=settings.root;GetSystemTime(&dump.started);dump.enabled=true;}}catch(...){}
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
 DrainDumps();
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
