// Synthetic startup reproducing issue #2: System32 VERSION.dll is loaded
// before application entry, so a bare-name request ignores the adjacent proxy.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
static HMODULE system_version;
static void NTAPI EarlyVersion(PVOID,DWORD reason,PVOID) {
  if(reason!=DLL_PROCESS_ATTACH)return;
  wchar_t path[MAX_PATH]{};
  GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\version.dll");
  system_version=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
}
extern "C" { PIMAGE_TLS_CALLBACK startup_callback __attribute__((section(".CRT$XLB"),used)) = EarlyVersion; }
int wmain() {
  if(!system_version || LoadLibraryW(L"version.dll")!=system_version)return 10;
  wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  std::wstring local=path;local.resize(local.find_last_of(L'\\')+1);local+=L"Floggfy.dll";
  auto proxy=GetModuleHandleW(local.c_str());
  if(!proxy || proxy==system_version)return 11;
  if(!GetProcAddress(proxy,"SpotifyConnectivityFixVersion"))return 12;
  if(IsDebuggerPresent())return 13;
  return 0;
}
