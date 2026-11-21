#define WIN32_LEAN_AND_MEAN
#include "windows_handle.h"
#include "startup_launcher.h"
#include "process_startup_loader.h"
#include "startup_ready.h"
#include "launcher_architecture.h"
#include <tlhelp32.h>
namespace startup { namespace {
using win32::Handle;
struct Child {
  PROCESS_INFORMATION process{};
  ~Child() {
    if(!process.hProcess)return;
    TerminateProcess(process.hProcess,ERROR_DLL_INIT_FAILED);
    DebugActiveProcessStop(process.dwProcessId);
    WaitForSingleObject(process.hProcess,2000);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
  }
};
static bool Same(const wchar_t* a,const std::wstring& b){return !_wcsicmp(a,b.c_str());}
static bool File(const std::wstring& path){auto a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);}
} // namespace
bool StopExisting(const std::wstring& exe,std::wstring& error) {
  const auto deadline=GetTickCount64()+10000;
  for(;;) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(snapshot.value==INVALID_HANDLE_VALUE){error=L"Cannot inspect running Spotify processes.";return false;}
    PROCESSENTRY32W p{};p.dwSize=sizeof(p);bool found=false;
    if(!Process32FirstW(snapshot.value,&p)){error=L"Cannot enumerate running processes.";return false;}
    do {
      if(_wcsicmp(p.szExeFile,L"Spotify.exe"))continue;
      Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID));
      if(!process.value) {
        if(GetLastError()==ERROR_INVALID_PARAMETER)continue; // Process already exited.
        error=L"Cannot access a running Spotify process (Windows error "+std::to_wstring(GetLastError())+L").";return false;
      }
      DWORD exit=STILL_ACTIVE;
      if(GetExitCodeProcess(process.value,&exit) && exit!=STILL_ACTIVE)continue;
      wchar_t path[32768]{};DWORD length=32768;
      if(!QueryFullProcessImageNameW(process.value,0,path,&length)) {
        if(GetExitCodeProcess(process.value,&exit) && exit!=STILL_ACTIVE)continue;
        error=L"Cannot identify a running Spotify installation.";return false;
      }
      if(!Same(path,exe))continue;
      Handle target(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,p.th32ProcessID));
      if(!target.value) {
        auto status=GetLastError();
        if(GetExitCodeProcess(process.value,&exit) && exit!=STILL_ACTIVE)continue;
        error=L"Cannot stop Spotify (Windows error "+std::to_wstring(status)+L").";return false;
      }
      // Revalidate the reopened handle before touching it, even if a PID changed.
      if(WaitForSingleObject(target.value,0)==WAIT_OBJECT_0)continue;
      length=32768;
      if(!QueryFullProcessImageNameW(target.value,0,path,&length)) {
        if(WaitForSingleObject(target.value,0)==WAIT_OBJECT_0)continue;
        error=L"Cannot validate the Spotify process to stop.";return false;
      }
      if(!Same(path,exe))continue;
      found=true;
      if(!TerminateProcess(target.value,0) && WaitForSingleObject(target.value,0)!=WAIT_OBJECT_0) {
        error=L"Cannot stop Spotify (Windows error "+std::to_wstring(GetLastError())+L").";return false;
      }
      const auto now=GetTickCount64();
      if(now>=deadline || WaitForSingleObject(target.value,static_cast<DWORD>(deadline-now))!=WAIT_OBJECT_0) {
        error=L"Stopping Spotify timed out.";return false;
      }
    }while(Process32NextW(snapshot.value,&p));
    if(GetLastError()!=ERROR_NO_MORE_FILES){error=L"Running-process enumeration failed.";return false;}
    if(!found)return true;
    if(GetTickCount64()>=deadline){error=L"Spotify kept restarting during shutdown.";return false;}
    // Recheck for children created while the original processes were exiting.
  }
}
bool Launch(const std::wstring& folder,PROCESS_INFORMATION& out,std::wstring& error) {
  out={};error.clear();const auto exe=folder+L"\\Spotify.exe",dll=folder+L"\\Floggfy.dll";
  if(!File(exe) || !File(dll)){error=L"Place Floggfy.exe and Floggfy.dll beside Spotify.exe.";return false;}
  if(File(folder+L"\\version.dll")){error=L"Launcher mode requires removing the automatic version.dll first. Install only one Floggfy mode.";return false;}
  if(!RequireX64(exe,L"Spotify.exe",error)||!RequireX64(dll,L"Floggfy.dll",error))return false;
  if(!StopExisting(exe,error))return false;
  auto command=L"\""+exe+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);
  Child child;auto& process=child.process;
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,folder.c_str(),&startup,&process)) {
    error=L"Spotify could not be started (Windows error "+std::to_wstring(GetLastError())+L").";return false;
  }
  if(!StopAtEntry(process)){error=L"Cannot stop Spotify safely before application startup.";return false;}
  wchar_t name[80]{};ReadyName(process.dwProcessId,name);
  Handle ready(CreateEventW(nullptr,TRUE,FALSE,name));
  if(!ready.value){error=L"Cannot create the Floggfy startup signal.";return false;}
  if(!LoadStoppedProcess(process,dll,error))return false;
  if(WaitForSingleObject(ready.value,10000)!=WAIT_OBJECT_0){error=L"Floggfy initialization timed out. Use Floggfy.dll from the same release as Floggfy.exe.";return false;}
  if(ResumeThread(process.hThread)==DWORD(-1)){error=L"Spotify could not resume after loading Floggfy.";return false;}
  out=process;child.process={};return true;
}
} // namespace startup
