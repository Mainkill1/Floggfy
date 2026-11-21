#include "telemetry_queue_exit.h"
#include "windows_last_error.h"
#include "telemetry_queue.h"
#include "async_log.h"
#include "vendor/minhook/include/MinHook.h"
#include "windows_known_folder.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
namespace privacy {
namespace {
struct ExitState {
    std::wstring users_root,log_path;
    unsigned log_level=0;
    decltype(&ExitProcess) original=nullptr;
    std::atomic<bool> entered{false};
};
// The callback and trampoline must outlive CRT teardown and any concurrent exit
// caller. These process-lifetime objects are intentionally not freed on detach.
std::atomic<ExitState*> exit_state{nullptr};
bool attempted=false,installed=false;
void Report(const ExitState& state,const QueueCleanupResult& cleanup) noexcept {
    const bool warning=cleanup.failed||cleanup.skipped;
    if(!state.log_level||(!warning&&state.log_level<2)||state.log_path.empty())return;
    try {
        SYSTEMTIME time{};GetSystemTime(&time);char line[384];
        std::snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02uZ %s privacy-queue normal-exit cleanup: removed=%u bytes=%llu failed=%u skipped=%u win32_error=%lu\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,warning?"warning":"activity",
            cleanup.removed,static_cast<unsigned long long>(cleanup.bytes),cleanup.failed,cleanup.skipped,cleanup.last_error);
        // Do not enqueue a log that the normal logger cannot drain before exit.
        // This bounded writer does not wait on the logger or create a worker.
        history::WriteCappedLog(state.log_path,line);
    }catch(...){/* Cleanup/logging failure must never prevent Spotify from exiting. */}
}
void WINAPI ExitWithQueueCleanup(UINT code) {
    const DWORD previous=GetLastError();
    auto* state=exit_state.load(std::memory_order_acquire);
    if(!state->entered.exchange(true,std::memory_order_acq_rel))
        Report(*state,ClearTrackingQueues(state->users_root,true));
    SetLastError(previous);
    state->original(code);
}
}
bool InstallQueueExitCleanup(bool enabled,const std::wstring& users_root,const std::wstring& log_path,unsigned log_level) noexcept {
    win32::LastError previous;
    if(!enabled)return true;
    if(attempted)return installed;
    attempted=true;
    try {
        if(users_root.empty())return false;
        auto state=std::make_unique<ExitState>();
        state->users_root=users_root;state->log_path=log_path;state->log_level=log_level;
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
        const auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"ExitProcess");
        if(!address)return false;
        void* target=nullptr;static_assert(sizeof(target)==sizeof(address));std::memcpy(&target,&address,sizeof(target));
        void* original=nullptr;
        if(MH_CreateHook(target,reinterpret_cast<void*>(ExitWithQueueCleanup),&original)!=MH_OK)return false;
        static_assert(sizeof(state->original)==sizeof(original));std::memcpy(&state->original,&original,sizeof(original));
        exit_state.store(state.release(),std::memory_order_release);
        const auto status=MH_EnableHook(target);
        installed=status==MH_OK||status==MH_ERROR_ENABLED;
        if(!installed)MH_DisableHook(target); // Retain potentially reachable state/trampoline.
        return installed;
    }catch(...){return false;}
}
bool InstallRoamingQueueExitCleanup(bool enabled,const std::wstring& log_path,unsigned log_level) noexcept {
    win32::LastError previous;
    if(!enabled)return true;
    try {
        std::wstring roaming;
        return SUCCEEDED(history::ReadKnownFolder(FOLDERID_RoamingAppData,roaming)) &&
            InstallQueueExitCleanup(true,roaming+L"\\Spotify\\Users",log_path,log_level);
    }catch(...){return false;}
}
}
