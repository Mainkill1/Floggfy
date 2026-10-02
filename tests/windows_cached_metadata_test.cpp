#define WIN32_LEAN_AND_MEAN
#include "../native/cached_metadata.h"
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
  assert(history::ReadStoredMetadata(root, live));
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
  assert(!history::ReadStoredMetadata(root, uncertain) && uncertain.fields.empty());
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
  auto previous = live.fields;
  live.uri = "spotify:track:0000000000000000000003";
  assert(!history::ReadStoredMetadata(root, live) && live.fields == previous);
  assert(!history::ReadStoredMetadata(root + L"\\missing", live) && live.fields == previous);
  std::cout
      << "PASS: read-only shared Windows cache lookup, exact identity and missing-cache fallback\n";
}
