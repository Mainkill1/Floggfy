#include "process_startup_loader.h"
#include "windows_handle.h"
#include <tlhelp32.h>
#include <cstdint>
#include <cwchar>
#include <utility>

namespace startup { namespace {
using win32::Handle;
bool Same(const wchar_t* a,const std::wstring& b){return !_wcsicmp(a,b.c_str());}
bool Read(HANDLE process,uintptr_t address,void* out,SIZE_T bytes) {
  SIZE_T read=0;
  return ReadProcessMemory(process,reinterpret_cast<void*>(address),out,bytes,&read)&&read==bytes;
}
bool WriteByte(HANDLE process,uintptr_t address,unsigned char value) {
  DWORD previous=0;auto target=reinterpret_cast<void*>(address);
  if(!VirtualProtectEx(process,target,1,PAGE_EXECUTE_READWRITE,&previous))return false;
  SIZE_T written=0;bool ok=WriteProcessMemory(process,target,&value,1,&written)&&written==1;
  DWORD ignored=0;ok=VirtualProtectEx(process,target,1,previous,&ignored)&&ok;
  return FlushInstructionCache(process,target,1)&&ok;
}
bool Module(DWORD pid,const std::wstring& name,bool full,uintptr_t& base,DWORD& size) {
  HANDLE value=INVALID_HANDLE_VALUE;
  for(unsigned attempt=0;attempt<200;++attempt) {
    value=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(value!=INVALID_HANDLE_VALUE)break;
    if(GetLastError()!=ERROR_BAD_LENGTH)return false;
    Sleep(5);
  }
  Handle snapshot(value);
  if(snapshot.value==INVALID_HANDLE_VALUE)return false;
  MODULEENTRY32W module{};module.dwSize=sizeof(module);
  if(Module32FirstW(snapshot.value,&module))do {
    if(Same(full?module.szExePath:module.szModule,name)) {
      base=reinterpret_cast<uintptr_t>(module.modBaseAddr);size=module.modBaseSize;return true;
    }
  }while(Module32NextW(snapshot.value,&module));
  return false;
}
} // namespace

bool StopAtEntry(PROCESS_INFORMATION& process) noexcept {
  uintptr_t entry=0;unsigned char original=0;bool patched=false,loader_break=false;
  const auto deadline=GetTickCount64()+15000;
  while(GetTickCount64()<deadline) {
    DEBUG_EVENT event{};
    if(!WaitForDebugEvent(&event,250)) {
      if(GetLastError()==ERROR_SEM_TIMEOUT)continue;
      return false;
    }
    DWORD status=DBG_CONTINUE;bool stopped=false,failed=false;
    if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
      if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
      auto base=reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage);
      IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 headers{};
      if(!Read(process.hProcess,base,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||
         dos.e_lfanew<=0||dos.e_lfanew>1024*1024||
         !Read(process.hProcess,base+dos.e_lfanew,&headers,sizeof(headers))||headers.Signature!=IMAGE_NT_SIGNATURE||
         headers.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||headers.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
         !headers.OptionalHeader.AddressOfEntryPoint||headers.OptionalHeader.AddressOfEntryPoint>=headers.OptionalHeader.SizeOfImage)failed=true;
      else entry=base+headers.OptionalHeader.AddressOfEntryPoint;
    } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT&&event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
    else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
      const auto& exception=event.u.Exception.ExceptionRecord;
      if(exception.ExceptionCode==EXCEPTION_BREAKPOINT&&!loader_break) {
        loader_break=true;
        patched=entry&&Read(process.hProcess,entry,&original,1)&&WriteByte(process.hProcess,entry,0xcc);
        failed=!patched;
      } else if(exception.ExceptionCode==EXCEPTION_BREAKPOINT&&patched&&
                reinterpret_cast<uintptr_t>(exception.ExceptionAddress)==entry&&event.dwThreadId==process.dwThreadId) {
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!WriteByte(process.hProcess,entry,original)||!GetThreadContext(process.hThread,&context))failed=true;
        else {
          context.Rip=entry;
          if(!SetThreadContext(process.hThread,&context)||SuspendThread(process.hThread)==DWORD(-1))failed=true;
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

bool LoadStoppedProcess(PROCESS_INFORMATION& process,const std::wstring& dll,std::wstring& error) noexcept {
  try {
    uintptr_t base=0;DWORD size=0;
    if(Module(process.dwProcessId,dll,true,base,size))return true;
    FARPROC function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE owner=nullptr;
    if(!function||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(function),&owner)) {error=L"Cannot resolve the Windows DLL loader.";return false;}
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(owner,path,32768)){error=L"Cannot identify the Windows loader module.";return false;}
    auto name=wcsrchr(path,L'\\');
    if(!name||!Module(process.dwProcessId,name+1,false,base,size)) {error=L"The Windows loader is unavailable in Spotify.";return false;}
    const auto offset=reinterpret_cast<uintptr_t>(function)-reinterpret_cast<uintptr_t>(owner);
    MEMORY_BASIC_INFORMATION memory{};auto remote=reinterpret_cast<void*>(base+offset);
    if(offset>=size||!VirtualQueryEx(process.hProcess,remote,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||
       (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {
      error=L"The Windows loader address could not be validated.";return false;
    }
    const auto bytes=(dll.size()+1)*sizeof(wchar_t);
    void* argument=VirtualAllocEx(process.hProcess,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!argument){error=L"Cannot allocate the DLL path in Spotify.";return false;}
    SIZE_T written=0;
    if(!WriteProcessMemory(process.hProcess,argument,dll.c_str(),bytes,&written)||written!=bytes) {
      VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);error=L"Cannot write the DLL path.";return false;
    }
    Handle thread(CreateRemoteThread(process.hProcess,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remote),argument,0,nullptr));
    if(!thread.value){VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);error=L"Cannot load Floggfy into Spotify.";return false;}
    if(WaitForSingleObject(thread.value,10000)!=WAIT_OBJECT_0) {error=L"Loading Floggfy timed out.";return false;}
    VirtualFreeEx(process.hProcess,argument,0,MEM_RELEASE);
    if(!Module(process.dwProcessId,dll,true,base,size)){error=L"Spotify did not load the adjacent Floggfy DLL.";return false;}
    return true;
  }catch(...){error=L"Loading Floggfy failed.";return false;}
}
} // namespace startup
