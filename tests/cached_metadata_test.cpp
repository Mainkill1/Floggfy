#include "../native/cached_metadata.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace history;
std::vector<uint8_t> Read(const std::string &name) {
  std::ifstream f(name, std::ios::binary);
  assert(f);
  return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : "build/cache-fixtures",
                    uri = "spotify:track:0000000000000000000001";
  for (const auto *name : {"table.ldb", "snappy.ldb"}) {
    CachedRecords records;
    assert(CollectMetadataTable(Read(root + "/" + name), uri, records));
    CachedTrackMetadata m;
    assert(DecodeCachedTrack(records, uri, m));
    assert(m.title == "Song" && m.album == "Album" && m.duration == 200);
    assert(m.fields.at("ARTIST") == "Lead; Guest" && m.fields.at("ALBUMARTIST") == "Lead; Guest");
    assert(m.fields.at("DATE") == "2024-05-06" && m.fields.at("YEAR") == "2024");
    assert(m.fields.at("LABEL") == "Example Records" &&
           m.fields.at("COPYRIGHT") == "2024 Example Publishing");
    assert(!m.fields.count("PUBLISHER")); // A record label is not an explicit publishing credit.
    RichMetadata live;
    live.uri = uri;
    live.title = "Song";
    live.album = "Album";
    live.artist = "Lead";
    live.duration = 200;
    live.fields["ARTIST"] = "Lead";
    assert(MergeCachedMetadata(m, live) && live.fields.at("ARTIST") == "Lead; Guest" &&
           live.artist == "Lead");
    auto stale = m;
    stale.fields["ARTIST"] = "Lead; Old Guest; Other";
    live.fields["ARTIST"] = "Lead; Current Guest";
    assert(MergeCachedMetadata(stale, live) && live.fields.at("ARTIST") == "Lead; Current Guest");
    live.duration = 215;
    assert(!MergeCachedMetadata(m, live));
    auto previous = records;
    assert(!CollectMetadataTable(Read(root + "/corrupt.ldb"), uri, records));
    assert(records.size() == previous.size());
    assert(CollectMetadataLog(Read(root + "/deleted.log"), uri, records));
    assert(!DecodeCachedTrack(records, uri, m));
  }
  for (const auto *name : {"complete.log", "fragmented.log", "truncated.log"}) {
    CachedRecords r;
    assert(CollectMetadataLog(Read(root + "/" + name), uri, r));
    CachedTrackMetadata m;
    assert(DecodeCachedTrack(r, uri, m));
  }
  CachedRecords repeated;
  assert(!CollectMetadataTable(Read(root + "/repeated-handle.ldb"), uri, repeated));
  assert(repeated.empty());
  assert(!CollectMetadataTable(Read(root + "/expansion-limit.ldb"), uri, repeated));
  assert(repeated.empty());
  CachedRecords mismatch;
  assert(CollectMetadataTable(Read(root + "/wrong-gid.ldb"), uri, mismatch));
  CachedTrackMetadata invalid;
  assert(!DecodeCachedTrack(mismatch, uri, invalid));
  CachedRecords partial, album;
  assert(CollectMetadataTable(Read(root + "/separate-album.ldb"), uri, partial));
  assert(DecodeCachedTrack(partial, uri, invalid) && invalid.fields.at("DATE") == "2024");
  const std::string album_uri = "spotify:album:0000000000000000000002";
  assert(CollectMetadataTable(Read(root + "/separate-album.ldb"), album_uri, album));
  std::map<std::string, std::string> complete;
  assert(DecodeCachedAlbum(album, album_uri, complete));
  assert(complete.at("DATE") == "2024-05-06" && complete.at("LABEL") == "Example Records");
  CachedRecords wrong;
  assert(CollectMetadataTable(Read(root + "/table.ldb"), "spotify:track:0000000000000000000003",
                              wrong));
  assert(wrong.empty());
  std::cout << "PASS: exact cached identity, signed protobuf fields, all artists, dates, labels, "
               "compressed tables, WAL fragments/tails, CRC rejection and deletion precedence\n";
}
