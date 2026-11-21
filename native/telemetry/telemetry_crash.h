#pragma once
#include <cstdint>
namespace privacy {
struct CrashStatus {
 bool requested=false;
 bool hook_active=false;
 std::uint64_t sanitized_launches=0;
 std::uint64_t launches_without_url=0;
 std::uint64_t rewrite_failures=0;
 std::uint64_t unmatched_launches=0;
};
// Restart-only. Queue alongside other startup hooks; caller owns ApplyQueued.
// Covers future matching CreateProcessW launches in this process only. Existing
// reporters, launches before activation, and other launch APIs remain uncovered.
bool PrepareCrash(bool enabled) noexcept;
void FinalizeCrash(bool applied) noexcept;
CrashStatus GetCrashStatus() noexcept;
}
