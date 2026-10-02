#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <string>
#include <cstdio>
#include <tlhelp32.h>
#include "../native/startup_launcher.h"
static bool AnyRunning(const std::wstring& exe) {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  assert(snapshot!=INVALID_HANDLE_VALUE);
  PROCESSENTRY32W item{};item.dwSize=sizeof(item);bool found=false;
  if(Process32FirstW(snapshot,&item))do {
    if(_wcsicmp(item.szExeFile,L"Spotify.exe"))continue;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,item.th32ProcessID);
    if(!process)continue;
    wchar_t name[32768]{};DWORD length=32768;
    if(QueryFullProcessImageNameW(process,0,name,&length) && !_wcsicmp(name,exe.c_str()))found=true;
    CloseHandle(process);
  }while(Process32NextW(snapshot,&item));
  CloseHandle(snapshot);return found;
}
int wmain(int argc,wchar_t** argv) {
  assert(argc==2);std::wstring folder=argv[1],exe=folder+L"\\Spotify.exe",dll=folder+L"\\version.dll";
  PROCESS_INFORMATION process{};std::wstring error;
  auto command=L"\""+exe+L"\"";STARTUPINFOW info{};info.cb=sizeof(info);
  // The issue's normal startup skips the adjacent DLL.
  assert(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,folder.c_str(),&info,&process));
  assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
  DWORD code=0;assert(GetExitCodeProcess(process.hProcess,&code) && code==11);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
  // The fallback loads it before the host entry point and detaches its debugger.
  if(!startup::Launch(folder,process,error))std::fwprintf(stderr,L"%ls\n",error.c_str());
  assert(process.hProcess);
  assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
  assert(GetExitCodeProcess(process.hProcess,&code) && code==0);
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
  assert(!AnyRunning(exe));
  // Missing and invalid DLLs must fail without leaving a child behind.
  assert(MoveFileW(dll.c_str(),(dll+L".saved").c_str()));
  assert(!startup::Launch(folder,process,error) && !process.hProcess && !error.empty());
  assert(!AnyRunning(exe));
  HANDLE file=CreateFileW(dll.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
  assert(file!=INVALID_HANDLE_VALUE);DWORD put=0;assert(WriteFile(file,"invalid",7,&put,nullptr));CloseHandle(file);
  assert(!startup::Launch(folder,process,error) && !process.hProcess);
  assert(!AnyRunning(exe));assert(DeleteFileW(dll.c_str()));
  // A valid DLL with no Floggfy readiness signal must time out and clean up.
  wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);wcscat_s(system,L"\\version.dll");
  assert(CopyFileW(system,dll.c_str(),TRUE));
  assert(!startup::Launch(folder,process,error) && !process.hProcess);
  assert(error.find(L"initialization timed out")!=std::wstring::npos && !AnyRunning(exe));
  assert(DeleteFileW(dll.c_str()));assert(MoveFileW((dll+L".saved").c_str(),dll.c_str()));
  // An existing instance is never killed or reused as a late-injection target.
  command=L"\""+exe+L"\"";
  PROCESS_INFORMATION existing{};
  assert(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,folder.c_str(),&info,&existing));
  assert(!startup::Launch(folder,process,error) && error.find(L"Quit Spotify")!=std::wstring::npos);
  assert(WaitForSingleObject(existing.hProcess,0)==WAIT_TIMEOUT);
  assert(TerminateProcess(existing.hProcess,0));assert(WaitForSingleObject(existing.hProcess,3000)==WAIT_OBJECT_0);
  CloseHandle(existing.hThread);CloseHandle(existing.hProcess);
  std::puts("PASS: System32-first bypass, pre-entry load, debugger detach, missing/invalid DLL, readiness timeout cleanup and existing-instance preservation");
  return 0;
}
