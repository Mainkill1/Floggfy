#include "telemetry_crash.h"
#include "../tooling/windows_last_error.h"
#include "../tooling/spotify_child_process.h"
#include "telemetry_crash_command.h"
#include "../vendor/minhook/include/MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstring>

// This file owns the single CreateProcessW detour. Launcher child loading and
// crash filtering share it so MinHook never stacks two hooks on one target.
// Crashpad handler_main.cc creates its upload thread only when options.url is
// nonempty, while its local exception handler/database still initialize:
// https://chromium.googlesource.com/crashpad/crashpad/+/refs/heads/main/handler/handler_main.cc
// The installed Spotify handler's --help confirms these documented URL, database
// and initial-client-data options. This hook changes no on-disk settings.
namespace privacy {
namespace {
using CreateProcess=decltype(&CreateProcessW);
std::atomic<CreateProcess> original_create{nullptr};
std::atomic<bool> requested{false},active{false};
std::atomic<std::uint64_t> sanitized{0},without_url{0},failures{0},unmatched{0};
std::wstring expected_handler;
void* hook_target=nullptr;
bool attempted=false,prepared=false;
std::wstring_view Filename(std::wstring_view path) {
 const auto slash=path.find_last_of(L"/\\");
 return slash==path.npos?path:path.substr(slash+1);
}
BOOL WINAPI CreateCrashProcess(LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process) {
 win32::LastError error;
 std::wstring replacement;
 bool changed=false,no_url=false;
 if(requested.load(std::memory_order_acquire)){
  try{
   const std::wstring executable=application?application:command?crash_command::ExecutableArgument(command):L"";
   if(crash_command::ExactExecutable(expected_handler,executable)){
    if(command){
     const auto result=crash_command::StripUploadUrl(command,replacement);
     changed=result==crash_command::Rewrite::Changed;
     no_url=result==crash_command::Rewrite::Unchanged;
     if(result==crash_command::Rewrite::Invalid)++failures;
    }else no_url=true;
   }else if(crash_command::ExactExecutable(L"crashpad_handler.exe",Filename(executable)))++unmatched;
  }catch(...){++failures;}
 }
 // Everything apart from the validated handler's URL argument is forwarded,
 // including caller-owned startup/process structures and native failure codes.
 SetLastError(error.value);
 const BOOL result=spotify_child::Create(original_create.load(std::memory_order_acquire),application,changed?replacement.data():command,
  process_attributes,thread_attributes,inherit,flags,environment,directory,startup,process);
 error.value=GetLastError();
 if(result){if(changed)++sanitized;else if(no_url)++without_url;}
 return result;
}
}
bool PrepareCrash(bool preference,HMODULE floggfy_module,int child_mode) noexcept {
 win32::LastError error;
 if(attempted)return false;
 attempted=true;requested.store(preference,std::memory_order_release);
 const bool load_children=spotify_child::Configure(floggfy_module,
   child_mode<0?spotify_child::ConfiguredMode(floggfy_module):child_mode);
 if(!preference&&!load_children)return false;
 try{
  wchar_t module[32768];
  const DWORD length=GetModuleFileNameW(nullptr,module,32768);
  if(!length||length>=32768)return false;
  const std::wstring_view host(module,length);
  if(!load_children&&!crash_command::ExactExecutable(L"Spotify.exe",Filename(host)))return false;
  const auto slash=host.find_last_of(L"/\\");
  if(slash==host.npos)return false;
  if(preference){expected_handler.assign(host.substr(0,slash+1));expected_handler+=L"crashpad_handler.exe";}
  const auto initialized=MH_Initialize();
  if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
  const auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateProcessW");
  if(!address)return false;
  static_assert(sizeof(address)==sizeof(hook_target));
  std::memcpy(&hook_target,&address,sizeof(hook_target));
  void* trampoline=nullptr;
  if(MH_CreateHook(hook_target,reinterpret_cast<void*>(CreateCrashProcess),&trampoline)!=MH_OK)return false;
  CreateProcess original=nullptr;
  static_assert(sizeof(original)==sizeof(trampoline));
  std::memcpy(&original,&trampoline,sizeof(original));
  original_create.store(original,std::memory_order_release);
  if(MH_QueueEnableHook(hook_target)!=MH_OK){
   MH_QueueDisableHook(hook_target);MH_RemoveHook(hook_target);return false;
  }
  prepared=true;return true;
 }catch(...){return false;}
}
void FinalizeCrash(bool applied) noexcept {
 win32::LastError error;
 if(!prepared)return;
 prepared=false;
 if(applied){active.store(true,std::memory_order_release);return;}
 // A failed shared application may have activated an earlier hook. Clear its
 // queued intent and disable it, retaining the trampoline for in-flight calls.
 MH_QueueDisableHook(hook_target);
 MH_DisableHook(hook_target);
 active.store(false,std::memory_order_release);
}
CrashStatus GetCrashStatus() noexcept {
 const auto child=spotify_child::GetStatus();
 return {requested.load(std::memory_order_acquire),active.load(std::memory_order_acquire),
  sanitized.load(std::memory_order_relaxed),without_url.load(std::memory_order_relaxed),
  failures.load(std::memory_order_relaxed),unmatched.load(std::memory_order_relaxed),
  child.enabled,child.loaded,child.failed,child.unsupported_debug};
}
}
