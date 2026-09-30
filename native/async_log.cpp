#define WIN32_LEAN_AND_MEAN
#include "async_log.h"
#include "history_settings.h"
#include <array>
#include <cstring>
#include <cstdio>
namespace history {namespace {
constexpr size_t cap=5*1024*1024;
struct Record {char event[16];char detail[768];};
static std::array<Record,128> queue;static size_t head=0,tail=0,count=0;
static SRWLOCK lock=SRWLOCK_INIT;static HANDLE signal=nullptr;
static void Push(const char* event,const char* text){
 if(!signal||!TryAcquireSRWLockExclusive(&lock))return;
 if(count<queue.size()){
  auto& r=queue[tail];snprintf(r.event,sizeof(r.event),"%s",event);snprintf(r.detail,sizeof(r.detail),"%s",text);
  for(char& c:r.detail){if(!c)break;if(static_cast<unsigned char>(c)<32)c=' ';}
  tail=(tail+1)%queue.size();++count;SetEvent(signal);
 }ReleaseSRWLockExclusive(&lock);
}
static DWORD WINAPI Run(LPVOID){
 for(;;){Record r{};bool got=false;AcquireSRWLockExclusive(&lock);if(count){r=queue[head];head=(head+1)%queue.size();--count;got=true;}ReleaseSRWLockExclusive(&lock);
  if(!got){WaitForSingleObject(signal,1000);continue;}
  auto s=GetSettings();if(!s.log||s.root.empty()||(std::strcmp(r.event,"debug")==0&&!s.debug_log))continue;
  if(!EnsureDirectory(s.root))continue;
  SYSTEMTIME t;GetSystemTime(&t);char line[1000];int n=snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02uZ %s %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,r.event,r.detail);
  if(n>0&&n<int(sizeof(line)))WriteCappedLog(s.root+L"\\Floggfy.log",std::string(line,size_t(n)));
 }
}
}
bool WriteCappedLog(const std::wstring& path,const std::string& line){
 if(line.empty()||line.size()>cap)return false;
 HANDLE h=CreateFileW(WindowsPath(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)return false;
 LARGE_INTEGER size{},pos{};bool ok=GetFileSizeEx(h,&size)!=0;
 if(ok){if(size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(cap-line.size())){pos.QuadPart=0;ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&SetEndOfFile(h);}else ok=SetFilePointerEx(h,pos,nullptr,FILE_END)!=0;}
 DWORD written=0;if(ok)ok=WriteFile(h,line.data(),DWORD(line.size()),&written,nullptr)&&written==line.size();CloseHandle(h);return ok;
}
void StartLogger(){if(signal)return;signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!signal)return;auto thread=CreateThread(nullptr,0,Run,nullptr,0,nullptr);if(thread){SetThreadPriority(thread,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(thread);}else{CloseHandle(signal);signal=nullptr;}}
void LogActivity(const char* event,const std::string& detail){if(LoggingEnabled())Push(event,detail.c_str());}
void QueueDiagnostic(const char* message){if(DebugLoggingEnabled())Push("debug",message);}
}
