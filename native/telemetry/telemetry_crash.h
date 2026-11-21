#pragma once
#include <windows.h>
#include <cstdint>
namespace privacy {
struct CrashStatus {
 bool requested=false;
 bool hook_active=false;
 std::uint64_t sanitized_launches=0;
 std::uint64_t launches_without_url=0;
 std::uint64_t rewrite_failures=0;
 std::uint64_t unmatched_launches=0;
 bool child_loading=false;
 std::uint64_t children_loaded=0;
 std::uint64_t child_failures=0;
 std::uint64_t unsupported_debug_launches=0;
};
// Restart-only. Queue the shared CreateProcessW hook used by launcher child
// loading and crash URL filtering; the caller owns ApplyQueued. Existing
// processes, launches before activation, and other launch APIs are uncovered.
bool PrepareCrash(bool enabled,HMODULE floggfy_module=nullptr) noexcept;
void FinalizeCrash(bool applied) noexcept;
CrashStatus GetCrashStatus() noexcept;
}
