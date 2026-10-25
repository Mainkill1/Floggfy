#define WIN32_LEAN_AND_MEAN
#include "async_log.h"
#include "history_settings.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include "networking_summary.h"
#include "hook_diagnostics.h"
#include <atomic>
namespace history {namespace {
constexpr size_t cap=5*1024*1024;
struct Record {
 char event[16]{};
 char detail[768]{};
 SYSTEMTIME utc{};
 ULONGLONG observed_ms=0,sequence=0;
};
struct LogQueue {
 std::array<Record,128> records{};
 size_t head=0,tail=0,count=0;
 ULONGLONG sequence=0;
 SRWLOCK lock=SRWLOCK_INIT;
};
static LogQueue activity_queue,network_queue;
static std::atomic<unsigned long> network_omitted{0};
static HANDLE signal=nullptr;
static void Push(LogQueue& queue,const char* event,const char* text,bool networking=false) noexcept {
 if(!signal)return;
 if(!TryAcquireSRWLockExclusive(&queue.lock)) {
  if(networking)network_omitted.fetch_add(1,std::memory_order_relaxed);
  return;
 }
 if(queue.count<queue.records.size()) {
  auto& record=queue.records[queue.tail];
  std::snprintf(record.event,sizeof(record.event),"%s",event);
  std::snprintf(record.detail,sizeof(record.detail),"%s",text);
  for(char& c:record.detail) {
   if(!c)break;
   if(static_cast<unsigned char>(c)<32)c=' ';
  }
  // Stamp enqueue time, not eventual disk-write time, to correlate delayed work.
  record.observed_ms=GetTickCount64();GetSystemTime(&record.utc);
  record.sequence=++queue.sequence;
  queue.tail=(queue.tail+1)%queue.records.size();++queue.count;
 }else if(networking)network_omitted.fetch_add(1,std::memory_order_relaxed);
 ReleaseSRWLockExclusive(&queue.lock);
 SetEvent(signal);
}
static bool Take(LogQueue& queue,Record& record) noexcept {
 AcquireSRWLockExclusive(&queue.lock);
 const bool ready=queue.count!=0;
 if(ready) {
  record=queue.records[queue.head];
  SecureZeroMemory(&queue.records[queue.head],sizeof(Record));
  queue.head=(queue.head+1)%queue.records.size();--queue.count;
 }
 ReleaseSRWLockExclusive(&queue.lock);return ready;
}
static std::string FormatLogRecord(const Record& record,bool networking) {
 const auto& t=record.utc;
 char line[1100];
 const int length=networking?std::snprintf(line,sizeof(line),
  "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu mono_ms=%llu seq=%llu %s\r\n",
  t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,
  GetCurrentProcessId(),record.observed_ms,record.sequence,record.detail):std::snprintf(line,sizeof(line),
  "%04u-%02u-%02uT%02u:%02u:%02uZ %s %s\r\n",
  t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,record.event,record.detail);
 return length>0&&length<int(sizeof(line))?std::string(line,size_t(length)):std::string{};
}
static void WriteRecord(const Record& record,bool networking) noexcept {
 try {
  const auto settings=GetSettings();
  if(!settings.log||settings.root.empty())return;
  if(networking?!NetworkingLoggingEnabled():std::strcmp(record.event,"debug")==0&&!settings.debug_log)return;
  if(!EnsureDirectory(settings.root))return;
  WriteCappedLog(settings.root+(networking?L"\\Floggfy-networking.log":L"\\Floggfy.log"),FormatLogRecord(record,networking));
 }catch(...) {/* Logging failures must not stop capture or either log queue. */}
}
static DWORD WINAPI Run(LPVOID) {
 for(;;) {
  PollHookDiagnostics();
  Record record;
  bool got=Take(activity_queue,record);
  if(got)WriteRecord(record,false);
  const bool network=Take(network_queue,record);
  if(network)WriteRecord(record,true);
  // Separate queues and fair draining prevent deep diagnostics from consuming
  // the normal activity queue. Omission records disclose incomplete evidence.
  const auto omitted=network_omitted.exchange(0,std::memory_order_relaxed);
  if(omitted&&NetworkingLoggingEnabled()) {
   char line[120];std::snprintf(line,sizeof(line),"direction=local source=logger observations_omitted=%lu",omitted);
   QueueNetworkingDiagnostic(line);
  }
  if(!got&&!network)WaitForSingleObject(signal,HookDiagnosticsEnabled()?500:1000);
 }
}
}
bool WriteCappedLog(const std::wstring& path,const std::string& line){
 if(line.empty()||line.size()>cap)return false;
 HANDLE h=CreateFileW(WindowsPath(path).c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)return false;
 struct FileOwner {HANDLE value;~FileOwner(){CloseHandle(value);}} owner{h};
 LARGE_INTEGER size{},pos{};bool ok=GetFileSizeEx(h,&size)!=0;
 if(ok && size.QuadPart<0)ok=false;
 if(ok && size.QuadPart>static_cast<LONGLONG>(cap-line.size())) {
  // Read only the bounded tail. Include the preceding byte so a tail that
  // already starts on a line boundary does not lose an extra complete line.
  const auto budget=cap-line.size();
  pos.QuadPart=size.QuadPart-static_cast<LONGLONG>(budget)-1;
  std::string tail(budget+1,'\0');DWORD read=0;
  ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&ReadFile(h,tail.data(),DWORD(tail.size()),&read,nullptr)&&read==tail.size();
  if(ok){
   auto boundary=tail.find('\n');
   tail=boundary==std::string::npos ? std::string{} : tail.substr(boundary+1);
   pos.QuadPart=0;DWORD retained=0;
   ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&WriteFile(h,tail.data(),DWORD(tail.size()),&retained,nullptr)&&retained==tail.size()&&SetEndOfFile(h);
  }
 } else if(ok) ok=SetFilePointerEx(h,pos,nullptr,FILE_END)!=0;
 DWORD written=0;if(ok)ok=WriteFile(h,line.data(),DWORD(line.size()),&written,nullptr)&&written==line.size();return ok;
}
void StartLogger(){if(signal)return;signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!signal)return;auto thread=CreateThread(nullptr,0,Run,nullptr,0,nullptr);if(thread){SetThreadPriority(thread,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(thread);
 QueueNetworkingDiagnostic("direction=local source=session coverage=decoded-connect,client-state,local-actions,own-enrichment unknown=other-traffic,command-execution mode=read-only raw-payloads=excluded credentials=excluded media=excluded");
}else{CloseHandle(signal);signal=nullptr;}}
void LogActivity(const char* event,const std::string& detail){if(LoggingEnabled())Push(activity_queue,event,detail.c_str());}
void QueueNetworkingDiagnostic(const char* message) noexcept {
 if(message&&NetworkingLoggingEnabled())Push(network_queue,"network",message,true);
}
void QueueDiagnostic(const char* message) {
 if(!message||!DebugLoggingEnabled())return;
 const std::string_view text(message,strnlen(message,768));
 const bool explicit_network=text.substr(0,19)=="FLOGGFY_NETWORK_V1:";
 if(explicit_network||(NetworkingLoggingEnabled()&&IsClientNetworking(text))) {
  if(NetworkingLoggingEnabled()) {
   std::string summary;
   if(SummarizeClientNetworking(text,summary))QueueNetworkingDiagnostic(summary.c_str());
  }
  return;
 }
 Push(activity_queue,"debug",message);
}
}
