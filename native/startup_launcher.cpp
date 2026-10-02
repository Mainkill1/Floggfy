#define WIN32_LEAN_AND_MEAN
#include "startup_launcher.h"
#include "startup_ready.h"
#include <tlhelp32.h>
#include <cstdint>
namespace startup { namespace {
struct Handle {
  HANDLE value=nullptr;
  explicit Handle(HANDLE h=nullptr):value(h){}
  ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
  Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
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
static bool Read(HANDLE process,uintptr_t address,void* out,SIZE_T n) {
  SIZE_T got=0;return ReadProcessMemory(process,reinterpret_cast<void*>(address),out,n,&got) && got==n;
}
static bool Byte(HANDLE process,uintptr_t address,unsigned char value) {
  DWORD old=0;auto p=reinterpret_cast<void*>(address);
  if(!VirtualProtectEx(process,p,1,PAGE_EXECUTE_READWRITE,&old))return false;
  SIZE_T put=0;bool ok=WriteProcessMemory(process,p,&value,1,&put) && put==1;
  DWORD ignored=0;ok=VirtualProtectEx(process,p,1,old,&ignored) && ok;
  return FlushInstructionCache(process,p,1) && ok;
}
static bool Module(DWORD pid,const std::wstring& name,bool full,uintptr_t& base,DWORD& size) {
  HANDLE value=INVALID_HANDLE_VALUE;
  // DLL initialization can change the loader list during a snapshot.
  // Microsoft documents ERROR_BAD_LENGTH as retryable; bound the wait.
  for(unsigned attempt=0;attempt<200;++attempt) {
    value=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(value!=INVALID_HANDLE_VALUE)break;
    if(GetLastError()!=ERROR_BAD_LENGTH)return false;
    Sleep(5);
  }
  Handle snapshot(value);
  if(snapshot.value==INVALID_HANDLE_VALUE)return false;
  MODULEENTRY32W m{};m.dwSize=sizeof(m);
  if(Module32FirstW(snapshot.value,&m))do {
    if(Same(full?m.szExePath:m.szModule,name)){base=reinterpret_cast<uintptr_t>(m.modBaseAddr);size=m.modBaseSize;return true;}
  }while(Module32NextW(snapshot.value,&m));
  return false;
}
static bool StopExisting(const std::wstring& exe,std::wstring& error) {
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
static bool EntryStop(PROCESS_INFORMATION& process) {
  uintptr_t entry=0;unsigned char original=0;bool patched=false,loader_break=false;
  const auto deadline=GetTickCount64()+15000;
  while(GetTickCount64()<deadline) {
    DEBUG_EVENT event{};
    if(!WaitForDebugEvent(&event,250)){if(GetLastError()==ERROR_SEM_TIMEOUT)continue;return false;}
    DWORD status=DBG_CONTINUE;bool stopped=false,failed=false;
    if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
      if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
      auto base=reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage);
      IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
      if(!Read(process.hProcess,base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
         dos.e_lfanew<=0 || dos.e_lfanew>1024*1024 ||
         !Read(process.hProcess,base+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
         nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
         !nt.OptionalHeader.AddressOfEntryPoint || nt.OptionalHeader.AddressOfEntryPoint>=nt.OptionalHeader.SizeOfImage)failed=true;
      else entry=base+nt.OptionalHeader.AddressOfEntryPoint;
    } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
    else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
      const auto& exception=event.u.Exception.ExceptionRecord;
      if(exception.ExceptionCode==EXCEPTION_BREAKPOINT && !loader_break) {
        loader_break=true;
        // Wait for the loader breakpoint before touching application code.
        patched=entry && Read(process.hProcess,entry,&original,1) && Byte(process.hProcess,entry,0xcc);
        failed=!patched;
      } else if(exception.ExceptionCode==EXCEPTION_BREAKPOINT && patched &&
                reinterpret_cast<uintptr_t>(exception.ExceptionAddress)==entry && event.dwThreadId==process.dwThreadId) {
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!Byte(process.hProcess,entry,original) || !GetThreadContext(process.hThread,&context))failed=true;
        else {
          context.Rip=entry;
          if(!SetThreadContext(process.hThread,&context) || SuspendThread(process.hThread)==DWORD(-1))failed=true;
          else stopped=true;
        }
      } else status=DBG_EXCEPTION_NOT_HANDLED;
    } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)failed=true;
    if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status))return false;
    if(failed)return false;
    if(stopped)return DebugActiveProcessStop(process.dwProcessId)!=FALSE;
  }
  return false;
}
static bool Load(PROCESS_INFORMATION& process,const std::wstring& dll,std::wstring& error) {
  uintptr_t base=0;DWORD size=0;
  if(Module(process.dwProcessId,dll,true,base,size))return true;
  FARPROC function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
  HMODULE owner=nullptr;
  if(!function || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
         reinterpret_cast<LPCWSTR>(function),&owner)){error=L"Cannot resolve the Windows DLL loader.";return false;}
  // Forwarded exports may live in KernelBase, not Kernel32. Rebase against
  // the actual owning module, never assume equal virtual addresses.
  wchar_t path[32768]{};
  if(!GetModuleFileNameW(owner,path,32768)){error=L"Cannot identify the Windows loader module.";return false;}
  auto name=wcsrchr(path,L'\\');if(!name || !Module(process.dwProcessId,name+1,false,base,size)) {
    error=L"The Windows loader is unavailable in Spotify.";return false;
  }
  const auto offset=reinterpret_cast<uintptr_t>(function)-reinterpret_cast<uintptr_t>(owner);
  MEMORY_BASIC_INFORMATION memory{};
  auto remote=reinterpret_cast<void*>(base+offset);
  if(offset>=size || !VirtualQueryEx(process.hProcess,remote,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
     (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {
    error=L"The Windows loader address could not be validated.";return false;
  }
  auto bytes=(dll.size()+1)*sizeof(wchar_t);
  void* argument=VirtualAllocEx(process.hProcess,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  if(!argument){error=L"Cannot allocate the DLL path in Spotify.";return false;}
  SIZE_T put=0;
  if(!WriteProcessMemory(process.hProcess,argument,dll.c_str(),bytes,&put) || put!=bytes) {
    VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);error=L"Cannot write the DLL path.";return false;
  }
  Handle thread(CreateRemoteThread(process.hProcess,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remote),argument,0,nullptr));
  if(!thread.value){VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);error=L"Cannot load Floggfy into Spotify.";return false;}
  auto result=WaitForSingleObject(thread.value,10000);
  if(result!=WAIT_OBJECT_0) {
    // Its argument is still in use. Launch() terminates only this new child;
    // do not free the buffer underneath an in-flight loader thread.
    error=L"Loading Floggfy timed out.";return false;
  }
  VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);
  // A thread exit code is only 32 bits, so verify the complete module path
  // rather than treating a truncated x64 HMODULE as the result.
  if(!Module(process.dwProcessId,dll,true,base,size)){error=L"Spotify did not load the adjacent Floggfy DLL.";return false;}
  return true;
}
} // namespace
bool Launch(const std::wstring& folder,PROCESS_INFORMATION& out,std::wstring& error) {
  out={};error.clear();const auto exe=folder+L"\\Spotify.exe",dll=folder+L"\\Floggfy.dll";
  if(!File(exe) || !File(dll)){error=L"Place Floggfy.exe and Floggfy.dll beside Spotify.exe.";return false;}
  if(File(folder+L"\\version.dll")){error=L"Launcher mode requires removing the automatic version.dll first. Install only one Floggfy mode.";return false;}
  if(!StopExisting(exe,error))return false;
  auto command=L"\""+exe+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);
  Child child;auto& process=child.process;
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,folder.c_str(),&startup,&process)) {
    error=L"Spotify could not be started (Windows error "+std::to_wstring(GetLastError())+L").";return false;
  }
  if(!EntryStop(process)){error=L"Cannot stop Spotify safely before application startup.";return false;}
  wchar_t name[80]{};ReadyName(process.dwProcessId,name);
  Handle ready(CreateEventW(nullptr,TRUE,FALSE,name));
  if(!ready.value){error=L"Cannot create the Floggfy startup signal.";return false;}
  if(!Load(process,dll,error))return false;
  if(WaitForSingleObject(ready.value,10000)!=WAIT_OBJECT_0){error=L"Floggfy initialization timed out. Use Floggfy.dll from the same release as Floggfy.exe.";return false;}
  if(ResumeThread(process.hThread)==DWORD(-1)){error=L"Spotify could not resume after loading Floggfy.";return false;}
  out=process;child.process={};return true;
}
} // namespace startup
