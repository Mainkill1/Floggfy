#include "spotify_child_process.h"
#include "process_startup_loader.h"
#include "startup_ready.h"
#include "windows_command_line.h"
#include "windows_last_error.h"
#include <array>
#include <cwchar>

namespace spotify_child { namespace {
std::wstring installation,dll_path;
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> loaded{0},failed{0},unsupported_debug{0};

std::wstring FullPath(std::wstring_view path) {
 if(path.empty())return {};
 const DWORD needed=GetFullPathNameW(std::wstring(path).c_str(),0,nullptr,nullptr);
 if(!needed)return {};
 std::wstring result(needed,L'\0');
 const DWORD length=GetFullPathNameW(std::wstring(path).c_str(),needed,result.data(),nullptr);
 if(!length||length>=needed)return {};
 result.resize(length);return result;
}
bool Same(std::wstring_view left,std::wstring_view right) {
 return left.size()==right.size()&&!_wcsnicmp(left.data(),right.data(),left.size());
}
bool CoveredExecutable(LPCWSTR application,LPCWSTR command) {
 std::wstring candidate=application?application:command?win32::command_line::Executable(command):L"";
 candidate=FullPath(candidate);if(candidate.empty())return false;
 const auto slash=candidate.find_last_of(L"/\\");
 if(slash==candidate.npos||!Same(candidate.substr(0,slash),installation))return false;
 const auto name=candidate.substr(slash+1);
 static constexpr std::array<const wchar_t*,5> approved={
  L"Spotify.exe",L"crashpad_handler.exe",L"SpotifyLauncher.exe",L"spotify_cli.exe",L"save-to-spotify.exe"
 };
 for(const auto expected:approved)if(!_wcsicmp(name.c_str(),expected))return true;
 return false;
}
void CloseAndClear(PROCESS_INFORMATION* process) {
 if(!process)return;
 if(process->hThread)CloseHandle(process->hThread);
 if(process->hProcess)CloseHandle(process->hProcess);
 *process={};
}
void StopChild(PROCESS_INFORMATION* process) {
 if(!process||!process->hProcess)return;
 TerminateProcess(process->hProcess,ERROR_DLL_INIT_FAILED);
 DebugActiveProcessStop(process->dwProcessId);
 WaitForSingleObject(process->hProcess,2000);
 CloseAndClear(process);
}
} // namespace

bool Configure(HMODULE module) noexcept {
 try {
  wchar_t path[32768]{};const DWORD length=module?GetModuleFileNameW(module,path,32768):0;
  if(!length||length>=32768)return false;
  std::wstring full(path,length);const auto slash=full.find_last_of(L"/\\");
  if(slash==full.npos||_wcsicmp(full.c_str()+slash+1,L"Floggfy.dll"))return false;
  installation=full.substr(0,slash);dll_path=std::move(full);
  enabled.store(true,std::memory_order_release);return true;
 }catch(...){return false;}
}
bool Enabled() noexcept {return enabled.load(std::memory_order_acquire);}
Status GetStatus() noexcept {
 return {Enabled(),loaded.load(std::memory_order_relaxed),failed.load(std::memory_order_relaxed),unsupported_debug.load(std::memory_order_relaxed)};
}

BOOL Create(CreateProcess original,LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process) noexcept {
 win32::LastError error;
 bool covered=false;
 try{covered=Enabled()&&CoveredExecutable(application,command);}
 catch(...){error.value=ERROR_NOT_ENOUGH_MEMORY;++failed;return FALSE;}
 if(!original||!covered) {
  error.Restore();
  const BOOL result=original?original(application,command,process_attributes,thread_attributes,inherit,flags,environment,directory,startup,process):FALSE;
  error.value=GetLastError();return result;
 }
 if(!process){error.value=ERROR_INVALID_PARAMETER;++failed;return FALSE;}
 if(flags&(DEBUG_PROCESS|DEBUG_ONLY_THIS_PROCESS)) {
  error.value=ERROR_NOT_SUPPORTED;++unsupported_debug;return FALSE;
 }
 const bool keep_suspended=(flags&CREATE_SUSPENDED)!=0;
 const DWORD controlled=(flags&~CREATE_SUSPENDED)|DEBUG_ONLY_THIS_PROCESS;
 error.Restore();
 if(!original(application,command,process_attributes,thread_attributes,inherit,controlled,environment,directory,startup,process)) {
  error.value=GetLastError();return FALSE;
 }
 const DWORD create_error=GetLastError();std::wstring message;
 if(!startup::StopAtEntry(*process)) {
  StopChild(process);error.value=ERROR_DLL_INIT_FAILED;++failed;return FALSE;
 }
 wchar_t ready_name[80]{};startup::ReadyName(process->dwProcessId,ready_name);
 HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,ready_name);
 const bool initialized=ready&&startup::LoadStoppedProcess(*process,dll_path,message)&&
                        WaitForSingleObject(ready,10000)==WAIT_OBJECT_0;
 if(ready)CloseHandle(ready);
 if(!initialized||(!keep_suspended&&ResumeThread(process->hThread)!=1)) {
  StopChild(process);error.value=ERROR_DLL_INIT_FAILED;++failed;return FALSE;
 }
 ++loaded;error.value=create_error;return TRUE;
}
} // namespace spotify_child
