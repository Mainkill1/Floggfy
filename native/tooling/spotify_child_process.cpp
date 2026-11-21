#include "spotify_child_process.h"
#include "process_startup_loader.h"
#include <cwchar>

namespace spotify_child { namespace {
std::wstring dll_path;
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> loaded{0},failed{0},unsupported_debug{0};
} // namespace

int ConfiguredMode(HMODULE module) noexcept {
 wchar_t path[32768]{};const DWORD length=module?GetModuleFileNameW(module,path,32768):0;
 if(!length||length>=32768)return 1;
 auto slash=wcsrchr(path,L'\\');if(!slash)return 1;
 if(wcscpy_s(slash+1,32768-(slash+1-path),L"SpotifyHistory.ini"))return 1;
 wchar_t value[8]{};GetPrivateProfileStringW(L"Misc",L"HookAllSubProcesses",L"1",value,8,path);
 if(!value[1]&&value[0]>=L'0'&&value[0]<=L'2')return value[0]-L'0';
 return 1;
}
bool Configure(HMODULE module,int mode) noexcept {
 try {
  if(mode<=0)return false;
  wchar_t path[32768]{};const DWORD length=module?GetModuleFileNameW(module,path,32768):0;
  if(!length||length>=32768)return false;
  std::wstring full(path,length);const auto slash=full.find_last_of(L"/\\");
  if(slash==full.npos||(mode==1&&_wcsicmp(full.c_str()+slash+1,L"Floggfy.dll")))return false;
  dll_path=std::move(full);
  enabled.store(true,std::memory_order_release);return true;
 }catch(...){return false;}
}
bool Enabled() noexcept {return enabled.load(std::memory_order_acquire);}
Status GetStatus() noexcept {
 return {Enabled(),loaded.load(std::memory_order_relaxed),failed.load(std::memory_order_relaxed),
         unsupported_debug.load(std::memory_order_relaxed)};
}

BOOL Create(CreateProcessFunction original,LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process) noexcept {
 if(!original||!Enabled()) {
  return original?original(application,command,process_attributes,thread_attributes,inherit,flags,environment,directory,startup,process):FALSE;
 }
 const BOOL result=startup::CreateWithDll(original,application,command,process_attributes,thread_attributes,
   inherit,flags,environment,directory,startup,process,dll_path);
 if(result)++loaded;
 else {++failed;if(GetLastError()==ERROR_NOT_SUPPORTED)++unsupported_debug;}
 return result;
}
} // namespace spotify_child
