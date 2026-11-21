// Uses real CreateProcessW and MinHook, but only starts copies of this fixture.
// A lost argument, disabled local handler, missed URL, or changed native error
// contract makes the child or parent fail; no Spotify crash is triggered.
#include "../native/telemetry_crash.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <vector>
namespace fixture {
SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
STARTUPINFOW startup{};
PROCESS_INFORMATION process{};
wchar_t environment[]=L"TEST=yes\0\0",directory[]=L"C:\\test",app[]=L"C:\\Other\\player.exe",command[]=L"player --url=kept";
BOOL return_value=FALSE;
BOOL WINAPI Forward(LPCWSTR a,LPWSTR c,LPSECURITY_ATTRIBUTES p,LPSECURITY_ATTRIBUTES t,
 BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW s,LPPROCESS_INFORMATION info) {
 assert(a==app&&c==command&&p==&attributes&&t==&attributes);
 assert(inherit==TRUE&&flags==(CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT));
 assert(env==environment&&cwd==directory&&s==&startup&&info==&process);
 assert(GetLastError()==42);info->dwProcessId=567;SetLastError(43);return return_value;
}
void CheckForwarding() {
 const auto saved=privacy::original_create.exchange(Forward);
 for(const BOOL value:{FALSE,TRUE}){
  return_value=value;process.dwProcessId=0;SetLastError(42);
  assert(privacy::CreateCrashProcess(app,command,&attributes,&attributes,TRUE,
   CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,environment,directory,&startup,&process)==value);
  assert(GetLastError()==43&&process.dwProcessId==567);
 }
 privacy::original_create.store(saved);
}
}
int wmain(int argc,wchar_t** argv) {
 if(argc>1&&!wcscmp(argv[1],L"--child")){
  bool database=false,client=false,url=false,expect_url=false;
  for(int i=2;i<argc;++i){
   database=database||!wcscmp(argv[i],L"--database=C:\\crash data");
   client=client||!wcscmp(argv[i],L"--initial-client-data=1,2,3");
   url=url||!wcsncmp(argv[i],L"--url",5);
   expect_url=expect_url||!wcscmp(argv[i],L"--expect-url");
  }
  return database&&client&&url==expect_url?0:21;
 }
 wchar_t module[32768];assert(GetModuleFileNameW(nullptr,module,32768));
 std::wstring self=module,folder=self.substr(0,self.find_last_of(L"\\")+1);
 const std::wstring child=folder+L"crashpad_handler.exe";
 assert(CopyFileW(self.c_str(),child.c_str(),FALSE));
 const bool enabled=!(argc>1&&!wcscmp(argv[1],L"--disabled"));
 const bool rollback_before=argc>1&&!wcscmp(argv[1],L"--rollback-before-apply");
 const bool rollback_after=argc>1&&!wcscmp(argv[1],L"--rollback-after-apply");
 const bool protected_launches=enabled&&!rollback_before&&!rollback_after;
 SetLastError(123);
 const bool ready=privacy::PrepareCrash(enabled);
 assert(GetLastError()==123);
 assert(ready==enabled);
 if(ready){
  if(!rollback_before)assert(MH_ApplyQueued()==MH_OK);
  SetLastError(123);privacy::FinalizeCrash(protected_launches);assert(GetLastError()==123);
  assert(MH_ApplyQueued()==MH_OK); // rollback must also clear queued intent
 }
 assert(privacy::GetCrashStatus().hook_active==protected_launches);
 auto launch=[&](const std::wstring& path,bool named,bool expect_url){
  std::wstring line=L"\""+path+L"\" --child --database=\"C:\\crash data\" --initial-client-data=1,2,3 --url=https://one --url \"https://two/a b\"";
  if(expect_url)line+=L" --expect-url";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  assert(CreateProcessW(named?path.c_str():nullptr,line.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process));
  assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
  DWORD code=99;assert(GetExitCodeProcess(process.hProcess,&code));
  CloseHandle(process.hThread);CloseHandle(process.hProcess);assert(code==0);
 };
 launch(child,true,!protected_launches);launch(child,false,!protected_launches);
 const auto lookalike=folder+L"crashpad_handler.exe.evil.exe";
 assert(CopyFileW(self.c_str(),lookalike.c_str(),FALSE));launch(lookalike,true,true);
 assert(DeleteFileW(lookalike.c_str()));
 assert(privacy::GetCrashStatus().sanitized_launches==(protected_launches?2u:0u));
 assert(DeleteFileW(child.c_str()));
 std::wstring invalid=L"\""+child+L"\" --url=https://one";
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 assert(!CreateProcessW(child.c_str(),invalid.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process));
 assert(GetLastError()==ERROR_FILE_NOT_FOUND);
 assert(privacy::GetCrashStatus().sanitized_launches==(protected_launches?2u:0u));
 fixture::CheckForwarding();
 puts("Windows crash launch tests passed");
 return 0;
}
