#include "../native/media/cached_metadata.h"
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
  const std::string album_uri = "spotify:album:0000000000000000000002";
  const CachedMetadataUris track_only{uri, {}}, uris{uri, album_uri};
  for (const auto *name : {"table.ldb", "snappy.ldb"}) {
    CachedMetadataRecords records;
    assert(CollectMetadataTable(Read(root + "/" + name), track_only, records));
    CachedTrackMetadata m;
    assert(DecodeCachedTrack(records.tracks, uri, m));
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
           live.artist == "Lead" && live.album_uri==m.album_uri &&
           live.fields.at("SPOTIFY_ALBUM_URI")==m.album_uri);
    auto stale = m;
    stale.fields["ARTIST"] = "Lead; Old Guest; Other";
    live.fields["ARTIST"] = "Lead; Current Guest";
    assert(MergeCachedMetadata(stale, live) && live.fields.at("ARTIST") == "Lead; Current Guest");
    live.duration = 215;
    assert(!MergeCachedMetadata(m, live));
    auto previous = records;
    assert(!CollectMetadataTable(Read(root + "/corrupt.ldb"), track_only, records));
    assert(records.tracks.size() == previous.tracks.size());
    assert(CollectMetadataLog(Read(root + "/deleted.log"), track_only, records));
    assert(!DecodeCachedTrack(records.tracks, uri, m));
  }
  for (const auto *name : {"complete.log", "fragmented.log", "truncated.log"}) {
    CachedMetadataRecords r;
    assert(CollectMetadataLog(Read(root + "/" + name), track_only, r));
    CachedTrackMetadata m;
    assert(DecodeCachedTrack(r.tracks, uri, m));
  }
  CachedMetadataRecords repeated;
  assert(!CollectMetadataTable(Read(root + "/repeated-handle.ldb"), uris, repeated));
  assert(repeated.tracks.empty() && repeated.albums.empty());
  assert(!CollectMetadataTable(Read(root + "/expansion-limit.ldb"), track_only, repeated));
  assert(repeated.tracks.empty());
  CachedMetadataRecords mismatch;
  assert(CollectMetadataTable(Read(root + "/wrong-gid.ldb"), track_only, mismatch));
  CachedTrackMetadata invalid;
  assert(!DecodeCachedTrack(mismatch.tracks, uri, invalid));
  CachedMetadataRecords records;
  const auto table = Read(root + "/separate-album.ldb");
  assert(CollectMetadataTable(table, uris, records));
  assert(records.tracks.size() == 1 && records.albums.size() == 1);
  assert(DecodeCachedTrack(records.tracks, uri, invalid) && invalid.fields.at("DATE") == "2024");
  std::map<std::string, std::string> complete;
  assert(DecodeCachedAlbum(records.albums, album_uri, complete));
  assert(complete.at("DATE") == "2024-05-06" && complete.at("LABEL") == "Example Records");

  CachedMetadataRecords full;
  for (unsigned i = 0; i < 31; ++i) {
    full.tracks[std::to_string(i)] = {};
    full.albums[std::to_string(i)] = {};
  }
  assert(CollectMetadataTable(table, uris, full));
  assert(full.tracks.size() == 32 && full.albums.size() == 32);
  // The table visits the album before the track. A full track map must roll both back.
  full.tracks.erase(records.tracks.begin()->first);
  full.tracks["last"] = {};
  full.albums.clear();
  assert(!CollectMetadataTable(table, uris, full));
  assert(full.albums.empty() && full.tracks.size() == 32 && full.tracks.count("last"));

  const auto updated = Read(root + "/paired.log");
  auto corrupt = updated;
  corrupt.insert(corrupt.end(), {0, 0, 0, 0, 0, 0, 1});
  assert(!CollectMetadataLog(corrupt, uris, records));
  assert(records.tracks.begin()->second.sequence == 100 &&
         records.albums.begin()->second.sequence == 100);
  assert(CollectMetadataLog(updated, uris, records));
  assert(CollectMetadataTable(table, uris, records));
  assert(records.tracks.begin()->second.sequence == 103 && records.tracks.begin()->second.deleted);
  assert(records.albums.begin()->second.sequence == 104 && records.albums.begin()->second.deleted);

  CachedMetadataRecords wrong;
  assert(CollectMetadataTable(table, {"spotify:track:0000000000000000000003", {}}, wrong));
  assert(wrong.tracks.empty() && wrong.albums.empty());
  std::cout << "PASS: exact cached identity, signed protobuf fields, all artists, dates, labels, "
               "paired table/WAL collection, per-target limits, rollback and deletion precedence\n";
}
