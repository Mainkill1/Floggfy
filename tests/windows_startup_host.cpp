// Synthetic startup reproducing issue #2: System32 VERSION.dll is loaded
// before application entry, so a bare-name request ignores the adjacent proxy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstring>
#include "../native/startup_ready.h"
static HMODULE system_version;
static void NTAPI EarlyVersion(PVOID,DWORD reason,PVOID) {
  if(reason!=DLL_PROCESS_ATTACH)return;
  wchar_t path[MAX_PATH]{};
  GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\version.dll");
  system_version=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
}
extern "C" { PIMAGE_TLS_CALLBACK startup_callback __attribute__((section(".CRT$XLB"),used)) = EarlyVersion; }
int wmain(int argc,wchar_t** argv) {
  if(!system_version || LoadLibraryW(L"version.dll")!=system_version)return 10;
  wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  std::wstring local=path;local.resize(local.find_last_of(L'\\')+1);
  if(argc==2 && !_wcsicmp(argv[1],L"--load-local-version")) {
    local+=L"version.dll";
    wchar_t name[80]{};startup::ReadyName(GetCurrentProcessId(),name);
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,name);if(!ready)return 20;
    auto second=LoadLibraryExW(local.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!second || second==system_version)return 21;
    if(WaitForSingleObject(ready,10000)!=WAIT_OBJECT_0)return 22;
    CloseHandle(ready);
    if(LoadLibraryW(L"version.dll")!=system_version)return 23;
    using Size=DWORD(WINAPI*)(LPCWSTR,LPDWORD);
    auto address=GetProcAddress(system_version,"GetFileVersionInfoSizeW");Size original=nullptr;memcpy(&original,&address,sizeof(original));
    address=GetProcAddress(second,"GetFileVersionInfoSizeW");Size forwarded=nullptr;memcpy(&forwarded,&address,sizeof(forwarded));
    wchar_t kernel[MAX_PATH]{};GetSystemDirectoryW(kernel,MAX_PATH);wcscat_s(kernel,L"\\kernel32.dll");
    DWORD ignored=0,expected=original?original(kernel,&ignored):0;
    if(!expected || !forwarded || forwarded(kernel,&ignored)!=expected)return 24;
    return 0;
  }
  local+=L"Floggfy.dll";
  auto proxy=GetModuleHandleW(local.c_str());
  if(!proxy || proxy==system_version)return 11;
  if(!GetProcAddress(proxy,"SpotifyConnectivityFixVersion"))return 12;
  if(IsDebuggerPresent())return 13;
  return 0;
}
