#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace history {
inline constexpr std::size_t kRecentDownloadLimit = 10;
struct RecentDownload {
    std::wstring title, path;
    std::uint64_t bytes = 0;
};
// Records a successfully published file for this process run. No filesystem I/O.
void RecordRecentDownload(const std::wstring& title, const std::wstring& path,
                          std::uint64_t bytes) noexcept;
// Never waits for another caller. False leaves out unchanged; retain the old snapshot.
bool ReadRecentDownloads(std::vector<RecentDownload>& out);
}
