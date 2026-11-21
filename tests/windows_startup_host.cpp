// Synthetic startup reproducing issue #2: System32 VERSION.dll is loaded
// before application entry, so a bare-name request ignores the adjacent proxy.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstring>
#include "../native/tooling/startup_ready.h"
static HMODULE system_version;
static void NTAPI EarlyVersion(PVOID,DWORD reason,PVOID) {
  if(reason!=DLL_PROCESS_ATTACH)return;
  wchar_t path[MAX_PATH]{};
  GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\version.dll");
  system_version=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
}
extern "C" { PIMAGE_TLS_CALLBACK startup_callback __attribute__((section(".CRT$XLB"),used)) = EarlyVersion; }
static bool HasModule(DWORD pid,const std::wstring& expected) {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
  if(snapshot==INVALID_HANDLE_VALUE)return false;
  MODULEENTRY32W module{};module.dwSize=sizeof(module);bool found=false;
  if(Module32FirstW(snapshot,&module))do {
    if(!_wcsicmp(module.szExePath,expected.c_str())){found=true;break;}
  }while(Module32NextW(snapshot,&module));
  CloseHandle(snapshot);return found;
}
static int RunChild(const std::wstring& exe,const std::wstring& line,DWORD flags,
                    const std::wstring& expected,bool expect_loaded,bool null_application=false) {
  std::wstring command=line;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(null_application?nullptr:exe.c_str(),command.data(),nullptr,nullptr,FALSE,flags,nullptr,nullptr,&startup,&process))return 30;
  if(flags&CREATE_SUSPENDED) {
    if(HasModule(process.dwProcessId,expected)!=expect_loaded)return 31;
    if(ResumeThread(process.hThread)!=1)return 32;
  }
  if(WaitForSingleObject(process.hProcess,10000)!=WAIT_OBJECT_0)return 33;
  DWORD code=99;if(!GetExitCodeProcess(process.hProcess,&code))return 34;
  CloseHandle(process.hThread);CloseHandle(process.hProcess);return static_cast<int>(code);
}
static bool NetworkBlocked() {
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;
  const SOCKET value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);const int error=WSAGetLastError();
  if(value!=INVALID_SOCKET)closesocket(value);
  WSACleanup();return value==INVALID_SOCKET&&error==WSAENETDOWN;
}
int wmain(int argc,wchar_t** argv) {
  if(!system_version || LoadLibraryW(L"version.dll")!=system_version)return 10;
  wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
  std::wstring folder=path;folder.resize(folder.find_last_of(L'\\')+1);
  if(argc==2 && !_wcsicmp(argv[1],L"--load-local-version")) {
    const std::wstring local=folder+L"version.dll";
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
    wchar_t child_test[4]{};
    if(GetEnvironmentVariableW(L"FLOGGFY_STARTUP_VERSION_CHILD_TEST",child_test,4)) {
      std::wstring exe=path,line=L"\""+exe+L"\" --type=utility --spawn-descendant";
      return RunChild(exe,line,0,local,true);
    }
    if(GetEnvironmentVariableW(L"FLOGGFY_STARTUP_VERSION_CHILD_DISABLED_TEST",child_test,4)) {
      std::wstring exe=path,line=L"\""+exe+L"\" --expect-unloaded";
      return RunChild(exe,line,0,local,false);
    }
    return 0;
  }
  const std::wstring local=folder+L"Floggfy.dll",automatic=folder+L"version.dll";
  auto proxy=GetModuleHandleW(L"Floggfy.dll");
  if(!proxy) {
    auto candidate=GetModuleHandleW(automatic.c_str());
    if(candidate&&GetProcAddress(candidate,"SpotifyConnectivityFixVersion"))proxy=candidate;
  }
  const bool loaded=proxy&&proxy!=system_version;
  if(argc>1&&!wcsncmp(argv[1],L"--type=",7)) {
    if(!loaded)return 11;
    wchar_t network_test[4]{};
    if(GetEnvironmentVariableW(L"FLOGGFY_STARTUP_NETWORK_TEST",network_test,4)&&!NetworkBlocked())return 15;
    if(argc>2&&!wcscmp(argv[2],L"--spawn-descendant")) {
      std::wstring exe=path,line=L"\""+exe+L"\" --type=renderer --loaded-check";
      return RunChild(exe,line,0,local,true);
    }
    return 0;
  }
  if(argc>1&&!wcscmp(argv[1],L"--expect-unloaded"))return loaded?14:0;
  if(!loaded)return 11;
  if(!GetProcAddress(proxy,"SpotifyConnectivityFixVersion"))return 12;
  if(IsDebuggerPresent())return 13;
  wchar_t enabled[4]{};
  if(GetEnvironmentVariableW(L"FLOGGFY_STARTUP_CHILD_DISABLED_TEST",enabled,4)) {
    const std::wstring lookalike=folder+L"Spotify.exe.evil.exe";
    if(!CopyFileW(path,lookalike.c_str(),FALSE))return 25;
    std::wstring line=L"\""+lookalike+L"\" --expect-unloaded";
    const int code=RunChild(lookalike,line,0,local,false);DeleteFileW(lookalike.c_str());
    return code;
  }
  if(GetEnvironmentVariableW(L"FLOGGFY_STARTUP_CHILD_TEST",enabled,4)) {
    const std::wstring exe=path,install=exe.substr(0,exe.find_last_of(L'\\')),other=install+L"\\other-install";
    std::wstring line=L"\""+exe+L"\" --type=network-service --spawn-descendant";
    int code=RunChild(exe,line,0,local,true);if(code)return 40+code;
    line=L"\""+exe+L"\" --type=network-service --loaded-check";
    code=RunChild(exe,line,CREATE_SUSPENDED,local,true);if(code)return 80+code;
    code=RunChild(exe,line,0,local,true,true);if(code)return 120+code;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION debugged{};
    std::wstring debug_line=line;SetLastError(ERROR_SUCCESS);
    if(CreateProcessW(exe.c_str(),debug_line.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,nullptr,&startup,&debugged)) {
      TerminateProcess(debugged.hProcess,0);CloseHandle(debugged.hThread);CloseHandle(debugged.hProcess);return 150;
    }
    if(GetLastError()!=ERROR_NOT_SUPPORTED||debugged.hProcess||debugged.hThread)return 151;
    const std::wstring lookalike=install+L"\\Spotify.exe.evil.exe";
    if(!CopyFileW(exe.c_str(),lookalike.c_str(),FALSE))return 152;
    line=L"\""+lookalike+L"\" --type=utility --loaded-check";
    code=RunChild(lookalike,line,0,local,true);DeleteFileW(lookalike.c_str());
    if(code)return 152+code;
    const std::wstring hidden=local+L".child-load-test";
    DeleteFileW(hidden.c_str());
    if(!MoveFileW(local.c_str(),hidden.c_str()))return 180;
    PROCESS_INFORMATION failed{};line=L"\""+exe+L"\" --type=network-service --loaded-check";
    SetLastError(ERROR_SUCCESS);
    const bool unexpectedly_created=CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&failed)!=FALSE;
    const DWORD failure_error=GetLastError();
    if(unexpectedly_created) {
      TerminateProcess(failed.hProcess,0);CloseHandle(failed.hThread);CloseHandle(failed.hProcess);
    }
    if(!MoveFileW(hidden.c_str(),local.c_str()))return 181;
    if(unexpectedly_created||failure_error!=ERROR_DLL_INIT_FAILED||failed.hProcess||failed.hThread)return 182;
    CreateDirectoryW(other.c_str(),nullptr);
    const std::wstring other_exe=other+L"\\Spotify.exe";
    if(!CopyFileW(exe.c_str(),other_exe.c_str(),FALSE))return 160;
    line=L"\""+other_exe+L"\" --type=utility --loaded-check";
    code=RunChild(other_exe,line,CREATE_SUSPENDED,local,true);DeleteFileW(other_exe.c_str());RemoveDirectoryW(other.c_str());
    if(code)return 160+code;
  }
  return 0;
}
