#define WIN32_LEAN_AND_MEAN
#include "../native/media/cached_metadata.h"
#include "../native/tooling/history_settings.h"
namespace history {
Settings settings;
Settings GetSettings() { return settings; }
void QueueDiagnostic(const char*) {}
}
#include <cassert>
#include <iostream>
#include <windows.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string input = argv[1];
  std::wstring root(input.begin(), input.end());
  history::RichMetadata live;
  live.uri = "spotify:track:0000000000000000000001";
  live.title = "Song";
  live.album = "Album";
  live.artist = "Lead";
  live.duration = 200;
  const auto damaged = root + L"\\synthetic-user\\primary.ldb\\000002.log";
  DeleteFileW(damaged.c_str()); // Remove only this test's previous failure fixture.
  const auto table = root + L"\\synthetic-user\\primary.ldb\\000001.ldb";
  HANDLE writer = CreateFileW(table.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  assert(writer != INVALID_HANDLE_VALUE);
  {
    // Padding before the footer leaves the real LevelDB blocks valid while
    // making two scans exceed a one-MiB read budget.
    const DWORD size=GetFileSize(writer,nullptr);
    std::vector<unsigned char> bytes(size);
    DWORD read=0;
    assert(ReadFile(writer,bytes.data(),size,&read,nullptr)&&read==size&&size>48);
    bytes.insert(bytes.end()-48,600*1024,0);
    const auto budget_root=root+L"-budget";
    const auto account=budget_root+L"\\synthetic-user";
    const auto directory=account+L"\\primary.ldb";
    const auto path=directory+L"\\000001.ldb";
    CreateDirectoryW(budget_root.c_str(),nullptr);
    CreateDirectoryW(account.c_str(),nullptr);
    CreateDirectoryW(directory.c_str(),nullptr);
    HANDLE padded=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
    assert(padded!=INVALID_HANDLE_VALUE);
    DWORD written=0;
    assert(WriteFile(padded,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size());
    CloseHandle(padded);
    history::settings.metadata_cache_read_mib=1;
    history::StoredMetadataRead result;
    auto known_album=live;
    known_album.album_uri="spotify:album:0000000000000000000002";
    assert(history::ReadStoredMetadata(budget_root,known_album,&result));
    assert(known_album.fields.at("DATE")=="2024-05-06");
    assert(result.read_bytes==bytes.size()&&result.budget_bytes==1024*1024);
    auto unknown_album=live;
    assert(!history::ReadStoredMetadata(budget_root,unknown_album,&result)&&unknown_album.fields.empty());
    assert(std::string(result.reason)=="read-budget"&&result.read_bytes==bytes.size()&&
           result.read_bytes<=result.budget_bytes);
    history::settings.metadata_cache_read_mib=2;
    assert(history::ReadStoredMetadata(budget_root,unknown_album,&result));
    assert(unknown_album.fields.at("DATE")=="2024-05-06");
    assert(result.read_bytes==2*bytes.size()&&result.budget_bytes==2*1024*1024);
    history::settings={};
    assert(DeleteFileW(path.c_str()));
    assert(RemoveDirectoryW(directory.c_str()));
    assert(RemoveDirectoryW(account.c_str()));
    assert(RemoveDirectoryW(budget_root.c_str()));
  }
  assert(history::ReadStoredMetadata(root, live));
  assert(live.album_uri=="spotify:album:0000000000000000000002"&&
         live.fields.at("SPOTIFY_ALBUM_URI")==live.album_uri);
  CloseHandle(writer);
  assert(live.fields.at("ARTIST") == "Lead; Guest" && live.fields.at("DATE") == "2024-05-06");
  assert(live.artist == "Lead" && !live.fields.count("PUBLISHER"));
  HANDLE log = CreateFileW(damaged.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           0, nullptr);
  assert(log != INVALID_HANDLE_VALUE);
  const unsigned char bad_record[] = {0, 0, 0, 0, 0, 0, 1};
  DWORD written = 0;
  assert(WriteFile(log, bad_record, sizeof(bad_record), &written, nullptr) &&
         written == sizeof(bad_record));
  CloseHandle(log);
  history::RichMetadata uncertain = live;
  uncertain.fields.clear();
  history::StoredMetadataRead result;
  assert(!history::ReadStoredMetadata(root, uncertain,&result) && uncertain.fields.empty());
  assert(std::string(result.reason)=="invalid-record"&&result.read_bytes>=sizeof(bad_record));
  assert(DeleteFileW(damaged.c_str()));
  log = CreateFileW(damaged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  assert(log != INVALID_HANDLE_VALUE);
  assert(WriteFile(log, bad_record, sizeof(bad_record), &written, nullptr) &&
         written == sizeof(bad_record));
  history::RichMetadata locked = live;
  locked.fields.clear();
  assert(history::ReadStoredMetadata(root, locked) && locked.fields.at("DATE") == "2024-05-06");
  CloseHandle(log);
  assert(DeleteFileW(damaged.c_str()));
  auto mismatch=live;
  mismatch.fields.clear();
  mismatch.album_uri="spotify:album:0000000000000000000003";
  assert(!history::ReadStoredMetadata(root,mismatch,&result)&&mismatch.fields.empty());
  assert(std::string(result.reason)=="identity-mismatch");
  HANDLE unreadable=CreateFileW(table.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
  assert(unreadable!=INVALID_HANDLE_VALUE);
  assert(!history::ReadStoredMetadata(root,mismatch,&result)&&mismatch.fields.empty());
  assert(std::string(result.reason)=="unreadable"&&result.read_bytes==0);
  CloseHandle(unreadable);
  auto previous = live.fields;
  live.uri = "spotify:track:0000000000000000000003";
  assert(!history::ReadStoredMetadata(root, live,&result) && live.fields == previous);
  assert(std::string(result.reason)=="missing-record");
  assert(!history::ReadStoredMetadata(root + L"\\missing", live,&result) && live.fields == previous);
  assert(std::string(result.reason)=="missing-cache"&&result.read_bytes==0);
  std::cout
      << "PASS: read-only shared Windows cache lookup, exact identity and missing-cache fallback\n";
}
