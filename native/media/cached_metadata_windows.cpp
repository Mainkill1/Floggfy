#define WIN32_LEAN_AND_MEAN
#include "cached_metadata.h"
#include "../tooling/history_settings.h"
#include "../tooling/windows_handle.h"
#include "../tooling/async_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../tooling/windows_known_folder.h"
#include <windows.h>

namespace history {
namespace {
// This is deliberately not a LevelDB database open: no LOCK, recovery, writes,
// API calls or Spotify service resolution. Only shared reads of existing files.
constexpr uint64_t max_file_bytes = 8 * 1024 * 1024;
struct File {
  std::wstring path;
  uint64_t size = 0, modified = 0;
  bool log = false;
};
class BackgroundIo {
  bool changed_ = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;

public:
  ~BackgroundIo() {
    if (changed_)
      SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
  }
};
std::vector<File> Files(const std::wstring &directory, const char* &failure) {
  std::vector<File> files;
  WIN32_FIND_DATAW data{};
  win32::FindHandle search{FindFirstFileW((directory + L"\\*").c_str(), &data)};
  if (search.value == INVALID_HANDLE_VALUE) {
    failure = win32::PathMissing(GetLastError())
        ? "missing-cache" : "unreadable";
    return files;
  }
  unsigned inspected = 0;
  do {
    if (++inspected > 512) {
      failure = "file-count";
      break;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    std::wstring name = data.cFileName;
    auto dot = name.rfind(L'.');
    if (dot == std::wstring::npos)
      continue;
    bool log = name.substr(dot) == L".log";
    if (!log && name.substr(dot) != L".ldb")
      continue;
    uint64_t size = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    if (size > max_file_bytes) {
      failure = "file-size";
      continue;
    }
    if (!size)
      continue;
    files.push_back(
        {directory + L"\\" + name, size,
         (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime,
         log});
  } while (FindNextFileW(search.value, &data));
  if (inspected <= 512 && GetLastError() != ERROR_NO_MORE_FILES)
    failure = "unreadable";
  std::sort(files.begin(), files.end(), [](const File &a, const File &b) {
    return a.modified != b.modified ? a.modified > b.modified : a.path > b.path;
  });
  if (files.size() > 128)
    failure = "file-count";
  return files;
}
bool Collect(const std::vector<File> &files, CachedMetadataUris uris, CachedMetadataRecords &records,
             StoredMetadataRead &result) {
  for (const auto &file : files) {
    if (file.size > result.budget_bytes - result.read_bytes) {
      result.reason = "read-budget";
      return false;
    }
    std::vector<uint8_t> bytes;
    {
      win32::Handle handle{CreateFileW(file.path.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
      if (handle.value == INVALID_HANDLE_VALUE) {
        // Spotify can exclusively hold its active WAL. In that case, use the
        // readable SST catalogue records and match them against the live track.
        if (file.log && GetLastError() == ERROR_SHARING_VIOLATION)
          continue;
        result.reason = "unreadable";
        return false;
      }
      // Read the enumerated prefix of files that Spotify may replace or append to.
      bytes.resize(size_t(file.size));
      DWORD got = 0;
      const bool ok = ReadFile(handle.value, bytes.data(), DWORD(bytes.size()), &got, nullptr) != 0;
      result.read_bytes += got;
      if (!ok || got != bytes.size()) {
        result.reason = "unreadable";
        return false;
      }
    }
    const bool valid = file.log ? CollectMetadataLog(bytes, uris, records)
                                : CollectMetadataTable(bytes, uris, records);
    if (!valid) {
      result.reason = "invalid-record";
      return false;
    }
  }
  return true;
}
} // namespace
bool ReadStoredMetadata(const std::wstring &users_root, RichMetadata &live,
                        StoredMetadataRead* diagnostic) {
  BackgroundIo background;
  StoredMetadataRead local;
  auto& result = diagnostic ? *diagnostic : local;
  result = {};
  result.budget_bytes = uint64_t(GetSettings().metadata_cache_read_mib) * 1024 * 1024;
  WIN32_FIND_DATAW data{};
  win32::FindHandle search{FindFirstFileW((users_root + L"\\*").c_str(), &data)};
  if (search.value == INVALID_HANDLE_VALUE) {
    result.reason = win32::PathMissing(GetLastError())
        ? "missing-cache" : "unreadable";
    return false;
  }
  unsigned accounts = 0;
  do {
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.')
      continue;
    if (++accounts > 8) {
      result.reason = "account-count";
      break;
    }
    const char* failure = nullptr;
    auto files = Files(users_root + L"\\" + data.cFileName + L"\\primary.ldb", failure);
    if (failure) {
      result.reason = failure;
      continue;
    }
    CachedMetadataRecords records;
    if (!Collect(files, {live.uri, live.album_uri}, records, result))
      continue;
    CachedTrackMetadata track;
    if (!DecodeCachedTrack(records.tracks, live.uri, track)) {
      const bool has_track = std::any_of(records.tracks.begin(), records.tracks.end(),
                                         [](const auto &item) { return !item.second.deleted; });
      result.reason = has_track ? "invalid-record" : "missing-record";
      continue;
    }
    if (track.title != live.title || track.album != live.album ||
        std::abs(track.duration - live.duration) > 1.0 ||
        (!live.album_uri.empty() && live.album_uri != track.album_uri)) {
      result.reason = "identity-mismatch";
      continue;
    }
    if (live.album_uri.empty()) {
      records.tracks.clear();
      if (!Collect(files, {{}, track.album_uri}, records, result))
        continue;
    }
    MergeCachedAlbum(records.albums, track);
    const bool merged = MergeCachedMetadata(track, live);
    result.reason = merged ? "found" : "identity-mismatch";
    return merged;
  } while (FindNextFileW(search.value, &data));
  if (accounts <= 8 && GetLastError()!=ERROR_NO_MORE_FILES)
    result.reason = "unreadable";
  return false;
}
void EnrichStoredMetadata(RichMetadata &live) {
  // The owning metadata record controls retries; each requested lookup reads
  // current local data within its configured byte budget.
  StoredMetadataRead result;
  std::wstring local;
  try {
    result.budget_bytes = uint64_t(GetSettings().metadata_cache_read_mib) * 1024 * 1024;
    if (SUCCEEDED(ReadKnownFolder(FOLDERID_LocalAppData, local))) {
      const auto root = local + L"\\Spotify\\Users";
      if (ReadStoredMetadata(root, live, &result))
        return;
    } else {
      result.reason = "missing-cache";
    }
  } catch (...) {
    result.reason = "resource-failure";
  }
  char line[320];
  std::snprintf(line,sizeof(line),"cached metadata uri=%.96s status=unavailable reason=%s readbytes=%llu budget=%llu",
                live.uri.c_str(),result.reason,static_cast<unsigned long long>(result.read_bytes),
                static_cast<unsigned long long>(result.budget_bytes));
  QueueDiagnostic(line);
}
} // namespace history
