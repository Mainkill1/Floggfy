#define WIN32_LEAN_AND_MEAN
#include "async_log.h"
#include "history_settings.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include "networking_summary.h"
#include "hook_diagnostics.h"
#include <atomic>
namespace history {namespace {
constexpr size_t cap=5*1024*1024,retained_tail=4*1024*1024,batch_size=16;
enum class LogLane {Activity,Diagnostic,Networking,Hook};
struct Record {
 char event[16]{};
 char detail[768]{};
 SYSTEMTIME utc{};
 ULONGLONG observed_ms=0,sequence=0;
};
struct LogQueue {
 std::array<Record,128> records{};
 size_t head=0,tail=0,count=0;
 ULONGLONG sequence=0,next_omission_report=0;
 std::atomic<unsigned long> omitted{0};
 SRWLOCK lock=SRWLOCK_INIT;
};
static LogQueue activity_queue,diagnostic_queue,networking_queue,hook_queue;
static HANDLE signal=nullptr;
static void Omit(LogQueue& queue) noexcept {
 auto value=queue.omitted.load(std::memory_order_relaxed);
 while(value!=std::numeric_limits<unsigned long>::max()&&
       !queue.omitted.compare_exchange_weak(value,value+1,std::memory_order_relaxed)){}
}
static void CopyText(char* target,size_t capacity,std::string_view source) noexcept {
 auto length=std::min(source.size(),capacity-1);
 // All native strings are UTF-8. Truncation must not split its final code point.
 if(length<source.size())while(length&&(static_cast<unsigned char>(source[length])&0xc0)==0x80)--length;
 if(length)std::memcpy(target,source.data(),length);
 target[length]=0;
 for(size_t i=0;i<length;++i)if(static_cast<unsigned char>(target[i])<32)target[i]=' ';
}
static void Stamp(Record& record) noexcept {
 record.observed_ms=GetTickCount64();GetSystemTime(&record.utc);
}
static void Push(LogQueue& queue,const char* event,std::string_view text) noexcept {
 if(!signal)return;
 if(!TryAcquireSRWLockExclusive(&queue.lock)){Omit(queue);return;}
 if(queue.count<queue.records.size()) {
  auto& record=queue.records[queue.tail];
  CopyText(record.event,sizeof(record.event),event?std::string_view(event):std::string_view{});
  CopyText(record.detail,sizeof(record.detail),text);
  // Stamp enqueue time, not eventual disk-write time, to correlate delayed work.
  Stamp(record);record.sequence=++queue.sequence;
  queue.tail=(queue.tail+1)%queue.records.size();++queue.count;
 }else Omit(queue);
 ReleaseSRWLockExclusive(&queue.lock);SetEvent(signal);
}
// Renderer input is claimed before filtering and strictly reconstructed before
// entering either queue. Disabling diagnostics cannot expose raw console text.
static void RouteRendererDiagnostic(std::string_view text) noexcept {
 if(!DebugLoggingEnabled())return;
 std::string summary;
 const auto kind=SanitizeRendererDiagnostic(text,summary);
 if(kind==RendererDiagnosticKind::Invalid)return;
 const bool networking=text.substr(0,19)=="FLOGGFY_NETWORK_V1:";
 if(networking&&!NetworkingLoggingEnabled())return;
 if((text.substr(0,23)=="FLOGGFY_STATUS:connect "||text.substr(0,23)=="FLOGGFY_STATUS:control ")&&!ConnectLoggingEnabled())return;
 Push(networking?networking_queue:diagnostic_queue,networking?"network":"debug",summary);
}
static size_t TakeBatch(LogQueue& queue,Record* records,size_t capacity) noexcept {
 AcquireSRWLockExclusive(&queue.lock);
 const auto count=std::min(queue.count,capacity);
 for(size_t i=0;i<count;++i) {
  records[i]=queue.records[queue.head];SecureZeroMemory(&queue.records[queue.head],sizeof(Record));
  queue.head=(queue.head+1)%queue.records.size();
 }
 queue.count-=count;ReleaseSRWLockExclusive(&queue.lock);return count;
}
[[maybe_unused]] static bool Take(LogQueue& queue,Record& record) noexcept {return TakeBatch(queue,&record,1)==1;}
struct LocalStamp {SYSTEMTIME time{};LONGLONG offset_minutes=0;};
static LocalStamp ToLocalStamp(const SYSTEMTIME& utc) noexcept {
 LocalStamp result;result.time=utc;
 if(!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&result.time))return result;
 FILETIME utc_file{},local_file{};
 if(!SystemTimeToFileTime(&utc,&utc_file)||!SystemTimeToFileTime(&result.time,&local_file))return result;
 ULARGE_INTEGER utc_ticks{},local_ticks{};
 utc_ticks.LowPart=utc_file.dwLowDateTime;utc_ticks.HighPart=utc_file.dwHighDateTime;
 local_ticks.LowPart=local_file.dwLowDateTime;local_ticks.HighPart=local_file.dwHighDateTime;
 result.offset_minutes=(LONGLONG(local_ticks.QuadPart)-LONGLONG(utc_ticks.QuadPart))/600000000LL;
 return result;
}
static std::string FormatLogRecord(const Record& record,bool diagnostics) {
 const auto stamp=ToLocalStamp(record.utc);const auto& t=stamp.time;
 const auto offset=stamp.offset_minutes<0?-stamp.offset_minutes:stamp.offset_minutes;char line[1100];
 const int length=diagnostics?std::snprintf(line,sizeof(line),
  "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02lld:%02lld pid=%lu mono_ms=%llu seq=%llu %s %s\r\n",
  t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,
  stamp.offset_minutes<0?'-':'+',offset/60,offset%60,
  GetCurrentProcessId(),record.observed_ms,record.sequence,record.event,record.detail):std::snprintf(line,sizeof(line),
  "%04u-%02u-%02uT%02u:%02u:%02u%c%02lld:%02lld %s %s\r\n",
  t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,
  stamp.offset_minutes<0?'-':'+',offset/60,offset%60,record.event,record.detail);
 return length>0&&length<int(sizeof(line))?std::string(line,size_t(length)):std::string{};
}
static bool LaneEnabled(LogLane lane) noexcept {
 if(lane==LogLane::Activity)return LoggingEnabled();
 if(lane==LogLane::Networking)return NetworkingLoggingEnabled();
 if(lane==LogLane::Hook)return HookDiagnosticsEnabled();
 return DebugLoggingEnabled();
}
static const wchar_t* LaneFile(LogLane lane) noexcept {
 if(lane==LogLane::Activity)return L"\\Floggfy.log";
 if(lane==LogLane::Networking)return L"\\Floggfy-networking.log";
 if(lane==LogLane::Hook)return L"\\Floggfy-hook-diagnostics.log";
 return L"\\Floggfy-diagnostics.log";
}
static const char* LaneName(LogLane lane) noexcept {
 if(lane==LogLane::Activity)return "activity";
 if(lane==LogLane::Networking)return "networking";
 if(lane==LogLane::Hook)return "hook";
 return "diagnostic";
}
static void WriteBatch(const Record* records,size_t count,LogLane lane) noexcept {
 try {
  const auto settings=GetSettings();
  if(settings.root.empty()||!LaneEnabled(lane))return;
  const bool diagnostics=lane!=LogLane::Activity;
  std::string batch;batch.reserve(count*1100);
  for(size_t i=0;i<count;++i)batch+=FormatLogRecord(records[i],diagnostics);
  if(batch.empty()||!EnsureDirectory(settings.root))return;
  WriteCappedLog(settings.root+LaneFile(lane),batch);
 }catch(...) {/* Logging failures must not stop capture or either log queue. */}
}
[[maybe_unused]] static void WriteRecord(const Record& record,LogLane lane) noexcept {WriteBatch(&record,1,lane);}
static bool Drain(LogQueue& queue,LogLane lane) noexcept {
 std::array<Record,batch_size+1> records{};
 auto count=TakeBatch(queue,records.data(),batch_size);
 const auto now=GetTickCount64();
 if(now>=queue.next_omission_report) {
  const auto omitted=queue.omitted.exchange(0,std::memory_order_relaxed);
  if(omitted) {
   auto& record=records[count++];Stamp(record);
   CopyText(record.event,sizeof(record.event),lane==LogLane::Activity?"warning":
       lane==LogLane::Networking?"network":lane==LogLane::Hook?"hook":"debug");
   std::snprintf(record.detail,sizeof(record.detail),"logger %s records omitted=%lu; evidence incomplete",LaneName(lane),omitted);
   // Queue sequences describe producer records only; zero identifies this
   // worker-generated aggregate. Never requeue it into a saturated lane.
   queue.next_omission_report=now+5000;
  }
 }
 if(count)WriteBatch(records.data(),count,lane);
 return count!=0;
}
static DWORD WINAPI Run(LPVOID) {
 for(;;) {
  PollHookDiagnostics();
  // A bounded batch from each lane keeps disk work fair. Optional observations
  // never reserve normal activity slots, including basic debug messages.
  const bool activity=Drain(activity_queue,LogLane::Activity);
  const bool diagnostics=Drain(diagnostic_queue,LogLane::Diagnostic);
  const bool networking=Drain(networking_queue,LogLane::Networking);
  const bool hook=Drain(hook_queue,LogLane::Hook);
  if(!activity&&!diagnostics&&!networking&&!hook)WaitForSingleObject(signal,HookDiagnosticsEnabled()?500:1000);
 }
}
}
bool WriteCappedLog(const std::wstring& path,const std::string& line){
 if(line.empty()||line.size()>cap||line.back()!='\n')return false;
 HANDLE h=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)return false;
 struct FileOwner {HANDLE value;~FileOwner(){CloseHandle(value);}} owner{h};
 LARGE_INTEGER size{},pos{};bool ok=GetFileSizeEx(h,&size)!=0;
 if(ok&&size.QuadPart<0)ok=false;
 if(ok&&size.QuadPart>static_cast<LONGLONG>(cap-line.size())) {
  // Reclaim ~1 MiB of append headroom. Cutting only after a newline preserves
  // complete records and UTF-8 boundaries; a record larger than the tail is lost.
  const auto budget=std::min(retained_tail,cap-line.size());std::string tail;
  if(budget) {
   pos.QuadPart=size.QuadPart-static_cast<LONGLONG>(budget)-1;
   tail.assign(budget+1,'\0');DWORD read=0;
   ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&ReadFile(h,tail.data(),DWORD(tail.size()),&read,nullptr)&&read==tail.size();
   if(ok){const auto boundary=tail.find('\n');tail=boundary==std::string::npos?std::string{}:tail.substr(boundary+1);}
  }
  if(ok) {
   pos.QuadPart=0;DWORD retained=0;
   ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&WriteFile(h,tail.data(),DWORD(tail.size()),&retained,nullptr)&&retained==tail.size()&&SetEndOfFile(h);
  }
 }else if(ok)ok=SetFilePointerEx(h,pos,nullptr,FILE_END)!=0;
 DWORD written=0;if(ok)ok=WriteFile(h,line.data(),DWORD(line.size()),&written,nullptr)&&written==line.size();return ok;
}
void StartLogger(){if(signal)return;signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!signal)return;auto thread=CreateThread(nullptr,0,Run,nullptr,0,nullptr);if(thread){SetThreadPriority(thread,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(thread);
 QueueNetworkingDiagnostic("direction=local source=session coverage=decoded-connect,client-state,local-actions,own-enrichment unknown=other-traffic,command-execution mode=read-only raw-payloads=excluded credentials=excluded media=excluded");
}else{CloseHandle(signal);signal=nullptr;}}
void LogEvent(LogKind kind,const char* event,const std::string& detail) {
 if(IsRendererDiagnostic(std::string_view(detail))) {RouteRendererDiagnostic(detail);return;}
 if(kind==LogKind::Diagnostic) {if(DebugLoggingEnabled())Push(diagnostic_queue,event,detail);return;}
 const auto required=kind==LogKind::Activity?2u:1u;
 if(LogLevel()<required)return;
 Push(activity_queue,kind==LogKind::Warning?"warning":kind==LogKind::Failure?"failed":event,detail);
}
void QueueNetworkingDiagnostic(const char* message) noexcept {
 if(!message)return;
 const std::string_view text(message,strnlen(message,768));
 if(IsRendererDiagnostic(text)){RouteRendererDiagnostic(text);return;}
 if(NetworkingLoggingEnabled())Push(networking_queue,"network",text);
}
void QueueHookDiagnostic(const char* message) noexcept {
 if(!message||!HookDiagnosticsEnabled())return;
 const std::string_view text(message,strnlen(message,768));Push(hook_queue,"hook",text);
}
bool QueueRendererDiagnostic(std::wstring_view message) noexcept {
 if(!IsRendererDiagnostic(message))return false;
 if(!DebugLoggingEnabled())return true;
 const bool request=message.substr(0,19)==L"FLOGGFY_NETWORK_V1:";
 if(message.size()>(request?159u:99u))return true;
 char ascii[160]{};
 for(size_t i=0;i<message.size();++i) {
  if(message[i]==0||message[i]>127){SecureZeroMemory(ascii,sizeof(ascii));return true;}
  ascii[i]=char(message[i]);
 }
 RouteRendererDiagnostic({ascii,message.size()});SecureZeroMemory(ascii,sizeof(ascii));return true;
}
void QueueDiagnostic(const char* message) {
 if(!message)return;
 const std::string_view text(message,strnlen(message,768));
 if(IsRendererDiagnostic(text)){RouteRendererDiagnostic(text);return;}
 if(DebugLoggingEnabled())Push(diagnostic_queue,"debug",text);
}
}
