#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace privacy {
struct QueueCleanupResult {
    unsigned removed=0,failed=0,skipped=0;
    std::uint64_t bytes=0;
    DWORD last_error=0;
};
// One-shot cleanup of the verified pending-tracking.pb queue only. The root
// parameter is the Spotify Users directory; it also isolates filesystem tests.
QueueCleanupResult ClearTrackingQueues(const std::wstring& users_root,bool enabled) noexcept;
QueueCleanupResult ClearRoamingTrackingQueues(bool enabled) noexcept;
}
