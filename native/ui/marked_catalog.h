#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace history {
void ConfigureMarkedCatalog(const std::wstring& root, bool enabled);
void QueueMarkedFile(const std::wstring& root, const std::wstring& path);
bool ToggleMarkedRebuild();
bool MarkedRebuildRunning();
std::uint64_t MarkedCatalogRevision();

struct MarkedQueryReply {
    std::string request;
    std::uint64_t revision = 0;
    std::vector<std::pair<std::string, std::string>> dates;
};
bool QueueMarkedQuery(const std::string& request, const std::vector<std::string>& uris,
                      void (*ready)() = nullptr);
bool TakeMarkedReply(MarkedQueryReply& reply);
void StopMarkedCatalog();
}
