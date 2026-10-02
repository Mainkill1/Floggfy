#define WIN32_LEAN_AND_MEAN
#include "cached_metadata.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <shlobj.h>
#include <windows.h>

namespace history {
namespace {
// This is deliberately not a LevelDB database open: no LOCK, recovery, writes,
// API calls or Spotify service resolution. Only shared reads of existing files.
constexpr uint64_t max_file_bytes = 8 * 1024 * 1024, max_lookup_bytes = 96 * 1024 * 1024;
struct File {
  std::wstring path;
  uint64_t size = 0, modified = 0;
  bool log = false;
};
struct Budget {
  uint64_t bytes = 0;
  ULONGLONG until = GetTickCount64() + 2000;
};
class FindHandle {
  HANDLE handle_;

public:
  explicit FindHandle(HANDLE h) : handle_(h) {}
  ~FindHandle() {
    if (handle_ != INVALID_HANDLE_VALUE)
      FindClose(handle_);
  }
};
class BackgroundIo {
  bool changed_ = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;

public:
  ~BackgroundIo() {
    if (changed_)
      SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
  }
};
std::vector<File> Files(const std::wstring &directory, bool &complete) {
  std::vector<File> files;
  WIN32_FIND_DATAW data{};
  HANDLE h = FindFirstFileW((directory + L"\\*").c_str(), &data);
  FindHandle guard(h);
  if (h == INVALID_HANDLE_VALUE) {
    complete = false;
    return files;
  }
  unsigned inspected = 0;
  do {
    if (++inspected > 512) {
      complete = false;
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
      complete = false;
      continue;
    }
    if (!size)
      continue;
    files.push_back(
        {directory + L"\\" + name, size,
         (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime,
         log});
  } while (FindNextFileW(h, &data));
  if (GetLastError() != ERROR_NO_MORE_FILES)
    complete = false;
  std::sort(files.begin(), files.end(), [](const File &a, const File &b) {
    return a.modified != b.modified ? a.modified > b.modified : a.path > b.path;
  });
  if (files.size() > 128)
    complete = false;
  return files;
}
bool Collect(const std::vector<File> &files, const std::string &uri, CachedRecords &records,
             Budget &budget) {
  for (const auto &file : files) {
    if (GetTickCount64() >= budget.until || file.size > max_lookup_bytes - budget.bytes)
      return false;
    HANDLE h = CreateFileW(file.path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      // Spotify can exclusively hold its active WAL. In that one case use
      // readable SST catalogue records, matched against the live track below.
      // This does not claim to reflect unreadable WAL updates or evictions.
      if (file.log && GetLastError() == ERROR_SHARING_VIOLATION)
        continue;
      return false;
    }
    // Files can be compacted, replaced or appended while Spotify is running.
    // Read the enumerated prefix; the parser accepts only verified complete records.
    std::vector<uint8_t> bytes;
    try {
      bytes.resize(size_t(file.size));
    } catch (...) {
      CloseHandle(h);
      throw;
    }
    DWORD got = 0;
    bool ok = ReadFile(h, bytes.data(), DWORD(bytes.size()), &got, nullptr) != 0;
    CloseHandle(h);
    budget.bytes += file.size;
    if (!ok || got != bytes.size())
      return false;
    bool valid = file.log ? CollectMetadataLog(bytes, uri, records)
                          : CollectMetadataTable(bytes, uri, records);
    if (!valid)
      return false;
  }
  return true;
}
} // namespace
bool ReadStoredMetadata(const std::wstring &users_root, RichMetadata &live) {
  BackgroundIo background;
  Budget budget;
  WIN32_FIND_DATAW data{};
  HANDLE h = FindFirstFileW((users_root + L"\\*").c_str(), &data);
  FindHandle guard(h);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  unsigned accounts = 0;
  do {
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.')
      continue;
    if (++accounts > 8 || GetTickCount64() >= budget.until)
      break;
    bool complete = true;
    auto files = Files(users_root + L"\\" + data.cFileName + L"\\primary.ldb", complete);
    if (!complete)
      continue;
    CachedRecords records;
    if (!Collect(files, live.uri, records, budget))
      continue;
    CachedTrackMetadata track;
    if (!DecodeCachedTrack(records, live.uri, track) || track.title != live.title ||
        track.album != live.album || std::abs(track.duration - live.duration) > 1.0)
      continue;
    CachedRecords albums;
    if (!Collect(files, track.album_uri, albums, budget))
      continue;
    std::map<std::string, std::string> fields;
    if (DecodeCachedAlbum(albums, track.album_uri, fields)) {
      RichMetadata embedded;
      embedded.uri = track.uri;
      embedded.title = track.title;
      embedded.album = track.album;
      embedded.duration = track.duration;
      embedded.fields = track.fields;
      CachedTrackMetadata complete = track;
      complete.fields = std::move(fields);
      MergeCachedMetadata(complete, embedded);
      track.fields = std::move(embedded.fields);
    }
    return MergeCachedMetadata(track, live);
  } while (FindNextFileW(h, &data));
  return false;
}
void EnrichStoredMetadata(RichMetadata &live) {
  // Only MetadataWorker calls this function. Bound both positive and negative
  // lookups so repeated renderer snapshots cannot cause repeated disk scans.
  struct Memo {
    std::string uri;
    CachedTrackMetadata value;
    ULONGLONG retry = 0;
    bool found = false;
  };
  static std::deque<Memo> memo;
  auto found =
      std::find_if(memo.begin(), memo.end(), [&](const Memo &m) { return m.uri == live.uri; });
  if (found != memo.end()) {
    if (found->found && GetTickCount64() < found->retry) {
      MergeCachedMetadata(found->value, live);
      return;
    }
    if (!found->found && GetTickCount64() < found->retry)
      return;
    memo.erase(found);
  }
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
    return;
  std::wstring root;
  try {
    root = std::wstring(local) + L"\\Spotify\\Users";
  } catch (...) {
    CoTaskMemFree(local);
    throw;
  }
  CoTaskMemFree(local);
  RichMetadata cached = live;
  cached.fields.clear();
  bool ok = ReadStoredMetadata(root, cached);
  Memo entry;
  entry.uri = live.uri;
  entry.retry = GetTickCount64() + (ok ? 300000 : 15000);
  entry.found = ok;
  if (ok) {
    entry.value.uri = cached.uri;
    entry.value.title = cached.title;
    entry.value.album = cached.album;
    entry.value.duration = cached.duration;
    entry.value.fields = std::move(cached.fields);
    MergeCachedMetadata(entry.value, live);
  }
  if (memo.size() >= 24)
    memo.pop_front();
  memo.push_back(std::move(entry));
}
} // namespace history
