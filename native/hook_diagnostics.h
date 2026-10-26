#pragma once
#include "diagnostic_activity.h"
#include "history_settings.h"
#include "async_log.h"
#include <cstdio>

namespace history {
inline diagnostics::ActivityTracker hook_activity;
inline std::atomic<std::uint64_t> capture_progress_ms{0};
// Records POD only. Never allocates, waits, formats, writes files, suspends
// threads or catches application exceptions from inside a client callback.
class HookDiagnosticScope {
    bool enabled_=false;
    diagnostics::ActivityToken token_;
public:
    explicit HookDiagnosticScope(diagnostics::Phase phase) noexcept {
        if(!HookDiagnosticsEnabled())return;
        enabled_=true;
        const auto error=GetLastError();
        token_=hook_activity.Begin(phase,GetCurrentThreadId(),GetTickCount64());SetLastError(error);
    }
    ~HookDiagnosticScope() {
        if(!enabled_)return;
        if(!token_.Valid())return;
        const auto error=GetLastError();hook_activity.Finish(token_,GetTickCount64());SetLastError(error);
    }
    HookDiagnosticScope(const HookDiagnosticScope&)=delete;
    HookDiagnosticScope& operator=(const HookDiagnosticScope&)=delete;
};
inline void NoteCaptureProgress() noexcept {
    if(!HookDiagnosticsEnabled())return;
    const auto error=GetLastError();capture_progress_ms.store(GetTickCount64(),std::memory_order_release);SetLastError(error);
}
// Logger worker only. Its own blocking I/O or a whole-process stall can delay
// these observations; this is not an independent OS hang/crash detector.
inline void PollHookDiagnostics() noexcept {
    if(!HookDiagnosticsEnabled())return;
    static std::uint64_t next_poll=0;
    static diagnostics::ProgressWatch progress;
    const auto now=GetTickCount64();if(now<next_poll)return;next_poll=now+250;
    hook_activity.Poll(now,[](const diagnostics::ActivityObservation& observation) {
        char line[260];std::snprintf(line,sizeof(line),
            "direction=local source=hook-diagnostic phase=%s state=%s thread=%u started_ms=%llu elapsed_ms=%llu",
            diagnostics::PhaseName(observation.phase),observation.pending?"still-active":"completed-slow",
            observation.thread,static_cast<unsigned long long>(observation.started_ms),
            static_cast<unsigned long long>(observation.elapsed_ms));QueueNetworkingDiagnostic(line);
    });
    const auto last=capture_progress_ms.load(std::memory_order_acquire);
    const auto state=progress.Observe(last,now);
    if(state!=diagnostics::ProgressState::None) {
        char line[200];std::snprintf(line,sizeof(line),
            "direction=local source=hook-diagnostic phase=capture-worker state=%s last_progress_ms=%llu age_ms=%llu",
            state==diagnostics::ProgressState::Stalled?"progress-gap":"recovered",
            static_cast<unsigned long long>(last),static_cast<unsigned long long>(now>=last?now-last:0));QueueNetworkingDiagnostic(line);
    }
    const auto omitted=hook_activity.TakeOmitted();
    if(omitted) {
        char line[130];std::snprintf(line,sizeof(line),"direction=local source=hook-diagnostic observations_omitted=%llu",
            static_cast<unsigned long long>(omitted));QueueNetworkingDiagnostic(line);
    }
}
}
