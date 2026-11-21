#include "../native/telemetry/telemetry_queue_exit.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

// The actual deletion and MinHook exit boundary are used. Only log storage is
// substituted so this test does not need the recording/settings infrastructure.
namespace history {
bool WriteCappedLog(const std::wstring& path,const std::string& line) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD wrote=0;const bool ok=WriteFile(file,line.data(),DWORD(line.size()),&wrote,nullptr)&&wrote==line.size();CloseHandle(file);return ok;
}
}
static std::wstring queue;
static void WriteQueue(){HANDLE f=CreateFileW(queue.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);DWORD n=0;assert(WriteFile(f,"queued at exit",14,&n,nullptr)&&n==14);CloseHandle(f);}
static bool Exists(const std::wstring& path){return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
int wmain(int argc,wchar_t** argv) {
    if(argc==3) {
        const std::wstring root=argv[2];queue=root+L"\\one-user\\pending-tracking.pb";
        const bool enabled=wcscmp(argv[1],L"disabled")!=0;
        SetLastError(123);
        assert(privacy::InstallQueueExitCleanup(enabled,root,root+L"\\exit.log",2));
        assert(GetLastError()==123);
        if(!wcscmp(argv[1],L"crt")) {assert(std::atexit(WriteQueue)==0);return 37;}
        WriteQueue();
        if(!wcscmp(argv[1],L"locked")) {
            HANDLE f=CreateFileW(queue.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);
            // Deliberately keep the lock until process exit: cleanup must fail
            // safely and preserve normal termination and its exit status.
        }
        if(!wcscmp(argv[1],L"forced")){assert(TerminateProcess(GetCurrentProcess(),37));Sleep(INFINITE);}
        ExitProcess(37);
    }
    wchar_t temp[32768],module[32768];assert(GetTempPathW(32768,temp));assert(GetModuleFileNameW(nullptr,module,32768));
    const auto root=std::wstring(temp)+L"Floggfy-queue-exit-"+std::to_wstring(GetCurrentProcessId());
    assert(CreateDirectoryW(root.c_str(),nullptr));assert(CreateDirectoryW((root+L"\\one-user").c_str(),nullptr));
    queue=root+L"\\one-user\\pending-tracking.pb";
    for(const auto mode:{L"disabled",L"normal",L"crt",L"locked",L"forced"}) {
        std::wstring command=L"\""+std::wstring(module)+L"\" "+mode+L" \""+root+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        assert(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
        assert(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0);
        DWORD code=0;assert(GetExitCodeProcess(process.hProcess,&code)&&code==37);CloseHandle(process.hThread);CloseHandle(process.hProcess);
        const bool cleaned=!wcscmp(mode,L"normal")||!wcscmp(mode,L"crt");
        assert(Exists(queue)!=cleaned);
        const auto log=root+L"\\exit.log";
        const bool logged=cleaned||!wcscmp(mode,L"locked");assert(Exists(log)==logged);
        if(Exists(queue))assert(DeleteFileW(queue.c_str()));
        if(Exists(log))assert(DeleteFileW(log.c_str()));
    }
    assert(RemoveDirectoryW((root+L"\\one-user").c_str()));assert(RemoveDirectoryW(root.c_str()));
    puts("Windows queue exit tests passed: normal/CRT late write, disabled, locked, forced exit and original exit status");
    return 0;
}
