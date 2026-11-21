#include "../native/telemetry/telemetry_queue.h"
#include <cassert>
#include <cstdio>
#include <string>

static std::wstring rename_source,rename_target;
static bool rename_checked=false;
static BOOL WINAPI GuardedDisposition(HANDLE file,FILE_INFO_BY_HANDLE_CLASS type,LPVOID info,DWORD size) {
    if(type==FileDispositionInfo&&!rename_source.empty()) {
        // Simulate a rename precisely between validation and deletion. The open
        // handle must forbid it so deletion cannot follow an unrelated new name.
        assert(!MoveFileW(rename_source.c_str(),rename_target.c_str()));
        assert(GetLastError()==ERROR_SHARING_VIOLATION);
        rename_checked=true;rename_source.clear();
    }
    return SetFileInformationByHandle(file,type,info,size);
}
#define SetFileInformationByHandle GuardedDisposition
#include "../native/telemetry/telemetry_queue.cpp"
#undef SetFileInformationByHandle

static void Make(const std::wstring& path) {assert(CreateDirectoryW(path.c_str(),nullptr));}
static void Write(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);
    DWORD wrote=0;assert(WriteFile(file,"fixture",7,&wrote,nullptr)&&wrote==7);CloseHandle(file);
}
static bool Exists(const std::wstring& path) {return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
int main() {
    wchar_t temp[32768];assert(GetTempPathW(32768,temp));
    const auto root=std::wstring(temp)+L"Floggfy-queue-test-"+std::to_wstring(GetCurrentProcessId());
    Make(root);Make(root+L"\\Users");Make(root+L"\\Users\\one-user");Make(root+L"\\Users\\two-user");
    Make(root+L"\\Users\\other");Make(root+L"\\Users\\one-user\\nested");
    const auto users=root+L"\\Users",one=users+L"\\one-user\\pending-tracking.pb",two=users+L"\\two-user\\pending-tracking.pb";
    const auto unrelated=users+L"\\one-user\\primary.ldb",backup=one+L".bak",other=users+L"\\other\\pending-tracking.pb",nested=users+L"\\one-user\\nested\\pending-tracking.pb";
    for(const auto& p:{one,two,unrelated,backup,other,nested})Write(p);
    auto result=privacy::ClearTrackingQueues(users,false);
    assert(!result.removed&&!result.failed&&Exists(one)&&Exists(two));
    HANDLE locked=CreateFileW(two.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(locked!=INVALID_HANDLE_VALUE);
    rename_source=one;rename_target=one+L".moved";
    SetLastError(123);result=privacy::ClearTrackingQueues(users,true);assert(GetLastError()==123);
    assert(rename_checked&&!Exists(rename_target));
    assert(result.removed==1&&result.bytes==7&&result.failed==1&&result.last_error==ERROR_SHARING_VIOLATION);
    assert(!Exists(one)&&Exists(two));
    for(const auto& p:{unrelated,backup,other,nested})assert(Exists(p));
    CloseHandle(locked);result=privacy::ClearTrackingQueues(users,true);
    assert(result.removed==1&&!result.failed&&!Exists(two));
    result=privacy::ClearTrackingQueues(users,true);assert(!result.removed&&!result.failed);
    result=privacy::ClearTrackingQueues(root+L"\\absent",true);assert(!result.removed&&!result.failed);
    // A directory with the queue filename must never be recursively removed.
    Make(one);result=privacy::ClearTrackingQueues(users,true);assert(result.skipped==1&&Exists(one));RemoveDirectoryW(one.c_str());
    // Junction creation does not require administrator or Developer Mode.
    Make(root+L"\\outside");Write(root+L"\\outside\\pending-tracking.pb");
    auto junction=users+L"\\redirect-user";
    auto command=L"cmd.exe /d /c mklink /J \""+junction+L"\" \""+root+L"\\outside\" >nul";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    assert(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);DWORD code=1;assert(GetExitCodeProcess(process.hProcess,&code)&&!code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    result=privacy::ClearTrackingQueues(users,true);assert(result.skipped==1&&Exists(root+L"\\outside\\pending-tracking.pb"));
    result=privacy::ClearTrackingQueues(junction,true);assert(result.skipped==1&&Exists(root+L"\\outside\\pending-tracking.pb"));
    assert(RemoveDirectoryW(junction.c_str()));
    for(const auto& p:{unrelated,backup,other,nested,root+L"\\outside\\pending-tracking.pb"})assert(DeleteFileW(p.c_str()));
    for(const auto& p:{root+L"\\outside",users+L"\\one-user\\nested",users+L"\\one-user",users+L"\\two-user",users+L"\\other",users,root})assert(RemoveDirectoryW(p.c_str()));
    puts("Windows queued telemetry cleanup tests passed");
}
