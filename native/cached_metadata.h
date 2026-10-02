#pragma once
#include "rich_metadata.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace history {
struct CachedRecord {
  std::uint64_t sequence = 0;
  bool deleted = false;
  std::string value;
};
using CachedRecords = std::map<std::string, CachedRecord>;
struct CachedTrackMetadata {
  std::string uri, title, album, album_uri;
  double duration = 0;
  std::map<std::string, std::string> fields;
};
bool CollectMetadataTable(const std::vector<std::uint8_t> &, const std::string &uri,
                          CachedRecords &);
bool CollectMetadataLog(const std::vector<std::uint8_t> &, const std::string &uri, CachedRecords &);
bool DecodeCachedTrack(const CachedRecords &, const std::string &uri, CachedTrackMetadata &);
bool DecodeCachedAlbum(const CachedRecords &, const std::string &uri,
                       std::map<std::string, std::string> &);
bool MergeCachedMetadata(const CachedTrackMetadata &, RichMetadata &);
// Windows implementation; called only from the metadata worker.
bool ReadStoredMetadata(const std::wstring &users_root, RichMetadata &);
void EnrichStoredMetadata(RichMetadata &);
} // namespace history
