// Spotify cache key selection and protobuf decoding. Schema reference:
// https://github.com/librespot-org/librespot/blob/dev/protocol/proto/metadata.proto
// Field numbers are checked alongside Any type and exact embedded GIDs.
#include "cached_metadata.h"
#include "../tooling/leveldb_reader.h"
#include <optional>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
namespace history {
namespace {
using Bytes = std::string_view;
using wire::Varint;
using wire::Sized;
constexpr size_t max_value = 131072;
constexpr Bytes track_prefix = "spotify:track:", album_prefix = "spotify:album:";
bool Wanted(Bytes key, Bytes uri) {
  constexpr Bytes prefix = "!xmeta#cache#";
  if (uri.empty() || key.substr(0, prefix.size()) != prefix)
    return false;

  if (key.back() != '#' || key.size() - 1 < uri.size())
    return false;
  key.remove_suffix(1);
  const auto uri_start = key.size() - uri.size();
  return uri_start >= 2 && key.substr(uri_start - 2, 2) == "#$" && key.substr(uri_start) == uri;
}
bool StoreRecord(CachedRecords &out, Bytes key, Bytes value, uint64_t sequence, bool deleted) {
  if (key.size() > 16384 || value.size() > max_value)
    return false;

  auto entry = out.lower_bound(key);
  if (entry == out.end() || entry->first != key) {
    if (out.size() >= 32)
      return false;
    entry = out.emplace_hint(entry, std::string(key), CachedRecord{});
  }
  auto &old = entry->second;
  if (sequence >= old.sequence)
    old = {sequence, deleted, std::string(value)};
  return true;
}
bool CollectRecords(const std::vector<uint8_t>& bytes, CachedMetadataUris uris,
                    CachedMetadataRecords& out, bool log) {
  std::optional<CachedMetadataRecords> staged;
  auto visit = [&](Bytes key, Bytes value, uint64_t sequence, bool deleted) {
    const bool track = Wanted(key, uris.track), album = Wanted(key, uris.album);
    if (!track && !album) return true;
    if (!staged) staged.emplace(out);
    return (!track || StoreRecord(staged->tracks, key, value, sequence, deleted)) &&
           (!album || StoreRecord(staged->albums, key, value, sequence, deleted));
  };
  const bool valid = log ? leveldb::VisitLog(bytes, visit) : leveldb::VisitTable(bytes, visit);
  if (valid && staged) out = std::move(*staged);
  return valid;
}
struct Field {
  unsigned key = 0, wire = 0;
  uint64_t number = 0;
  Bytes bytes;
};
using Message = std::vector<Field>;
bool Proto(Bytes b, Message &out) {
  if (b.size() > max_value)
    return false;
  size_t p = 0;
  out.clear();
  while (p < b.size()) {
    uint64_t tag;
    if (out.size() >= 4096 || !Varint(b, p, tag) || !(tag >> 3) || tag >> 3 > 0x1fffffff)
      return false;

    Field f;
    f.key = unsigned(tag >> 3);
    f.wire = unsigned(tag & 7);
    if (f.wire == 0) {
      if (!Varint(b, p, f.number))
        return false;
    } else if (f.wire == 2) {
      if (!Sized(b, p, f.bytes))
        return false;
    } else if (f.wire == 1 || f.wire == 5) {
      size_t n = f.wire == 1 ? 8 : 4;
      if (n > b.size() - p)
        return false;
      p += n;
    } else
      return false;

    out.push_back(f);
  }
  return true;
}
const Field *Get(const Message &m, unsigned key, unsigned wire) {
  for (auto i = m.rbegin(); i != m.rend(); ++i)
    if (i->key == key && i->wire == wire)
      return &*i;
  return nullptr;
}
Bytes Data(const Message &m, unsigned key) {
  auto f = Get(m, key, 2);
  return f ? f->bytes : Bytes{};
}
uint64_t Number(const Message &m, unsigned key) {
  auto f = Get(m, key, 0);
  return f ? f->number : 0;
}
int64_t Signed(const Message &m, unsigned key) {
  uint64_t n = Number(m, key);
  return int64_t(n >> 1) ^ -int64_t(n & 1);
}
std::string Text(const Message &m, unsigned key) {
  auto b = Data(m, key);
  if (b.empty() || b.size() > 4096 || !MetadataTextValid(b))
    return {};
  return std::string(b);
}
std::string UriFromGid(Bytes gid, Bytes prefix) {
  static constexpr char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::array<uint8_t, 16> digits;
  if (gid.size() != digits.size())
    return {};
  std::copy(gid.begin(), gid.end(), digits.begin());
  std::string uri(prefix);
  uri.resize(prefix.size() + 22);
  for (size_t i = uri.size(); i > prefix.size(); --i) {
    unsigned remainder = 0;
    for (auto &byte : digits) {
      unsigned value = (remainder << 8) | byte;
      byte = uint8_t(value / 62);
      remainder = value % 62;
    }
    uri[i - 1] = alphabet[remainder];
  }
  return uri;
}
void Put(std::map<std::string, std::string> &out, const char *tag, const std::string &value) {
  if (!value.empty() && value.size() <= 4096 && MetadataTextValid(value))
    out[tag] = value;
}
std::string Join(const std::vector<std::string> &values) {
  std::string joined;
  for (const auto &v : values) {
    if (!joined.empty())
      joined += "; ";
    joined += v;
  }
  return joined;
}
void Add(std::vector<std::string> &values, const std::string &value) {
  if (!value.empty() && values.size() < 32 &&
      std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}
template <class Visitor> void VisitMessages(const Message &m, unsigned key, Visitor visit) {
  Message nested;
  for (const auto &f : m)
    if (f.key == key && f.wire == 2 && Proto(f.bytes, nested))
      visit(nested);
}
std::vector<std::string> Artists(const Message &m, unsigned key) {
  std::vector<std::string> names;
  VisitMessages(m, key, [&](const Message &artist) { Add(names, Text(artist, 2)); });
  return names;
}
void AlbumFields(const Message &album, std::map<std::string, std::string> &out) {
  Put(out, "ALBUMARTIST", Join(Artists(album, 3)));
  auto label = Text(album, 5);
  Put(out, "LABEL", label);
  Put(out, "ORGANIZATION", label);
  Message date;
  if (Proto(Data(album, 6), date)) {
    auto y = Signed(date, 1), m = Signed(date, 2), d = Signed(date, 3);
    std::string value;
    if (y >= 1000 && y <= 9999) {
      value = std::to_string(y);
      if (m >= 1 && m <= 12) {
        char part[16];
        snprintf(part, sizeof(part), "-%02d", int(m));
        value += part;
        constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int maximum = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        if (d >= 1 && d <= maximum) {
          snprintf(part, sizeof(part), "-%02d", int(d));
          value += part;
        }
      }
      Put(out, "DATE", value);
      Put(out, "YEAR", std::to_string(y));
    }
  }
  std::vector<std::string> copyright, phonogram;
  VisitMessages(album, 13, [&](const Message &notice) {
    const auto kind = Number(notice, 1);
    if (kind == 1)
      Add(copyright, Text(notice, 2));
    else if (kind == 0)
      Add(phonogram, Text(notice, 2));
  });
  Put(out, "COPYRIGHT", Join(copyright));
  Put(out, "PHONOGRAMCOPYRIGHT", Join(phonogram));
}
bool Payload(const CachedRecord &r, const char *type, Message &out) {
  if (r.deleted)
    return false;
  Message envelope, any;
  if (!Proto(r.value, envelope) || !Proto(Data(envelope, 2), any))
    return false;

  auto name = Text(any, 1);
  const auto slash = name.rfind('/');
  return slash != std::string::npos && name.substr(slash + 1) == type && Proto(Data(any, 2), out);
}
} // namespace
bool CollectMetadataTable(const std::vector<uint8_t>& bytes, CachedMetadataUris uris,
                          CachedMetadataRecords& out) {
  return CollectRecords(bytes, uris, out, false);
}
bool CollectMetadataLog(const std::vector<uint8_t>& bytes, CachedMetadataUris uris,
                        CachedMetadataRecords& out) {
  return CollectRecords(bytes, uris, out, true);
}
bool DecodeCachedTrack(const CachedRecords &records, const std::string &uri,
                       CachedTrackMetadata &out) {
  if (!ValidSpotifyUri(uri, track_prefix))
    return false;
  std::optional<uint64_t> latest;
  CachedTrackMetadata result;
  for (const auto &[key, record] : records) {
    Message track, album;
    if ((latest && record.sequence < *latest) || !Payload(record, "spotify.metadata.Track", track) ||
        UriFromGid(Data(track, 1), track_prefix) != uri ||
        !Proto(Data(track, 3), album))
      continue;
    CachedTrackMetadata m;
    m.uri = uri;
    m.title = Text(track, 2);
    m.album = Text(album, 2);
    m.album_uri = UriFromGid(Data(album, 1), album_prefix);
    m.duration = double(Signed(track, 7)) / 1000;
    if (m.title.empty() || m.album.empty() || m.album_uri.empty() || !std::isfinite(m.duration) ||
        m.duration <= 0 || m.duration > 86400)
      continue;
    Put(m.fields, "ARTIST", Join(Artists(track, 4)));
    AlbumFields(album, m.fields);
    VisitMessages(track, 10, [&](const Message &external) {
      if (Text(external, 1) == "isrc")
        Put(m.fields, "ISRC", Text(external, 2));
    });
    auto number = Signed(track, 5), disc = Signed(track, 6);
    if (number > 0 && number < 100000)
      Put(m.fields, "TRACKNUMBER", std::to_string(number));
    if (disc > 0 && disc < 100000)
      Put(m.fields, "DISCNUMBER", std::to_string(disc));
    std::vector<std::string> composer, remixer, conductor;
    VisitMessages(track, 32, [&](const Message &role) {
      auto name = Text(role, 2);
      switch (Number(role, 3)) {
      case 3:
        Add(remixer, name);
        break;
      case 5:
        Add(composer, name);
        break;
      case 6:
        Add(conductor, name);
        break;
      }
    });
    Put(m.fields, "COMPOSER", Join(composer));
    Put(m.fields, "REMIXER", Join(remixer));
    Put(m.fields, "CONDUCTOR", Join(conductor));
    latest = record.sequence;
    result = std::move(m);
  }
  if (latest)
    out = std::move(result);
  return latest.has_value();
}
bool DecodeCachedAlbum(const CachedRecords &records, const std::string &uri,
                       std::map<std::string, std::string> &out) {
  if (!ValidSpotifyUri(uri, album_prefix))
    return false;
  std::optional<uint64_t> latest;
  std::map<std::string, std::string> result;
  for (const auto &[key, record] : records) {
    Message album;
    if ((latest && record.sequence < *latest) || !Payload(record, "spotify.metadata.Album", album) ||
        UriFromGid(Data(album, 1), album_prefix) != uri)
      continue;
    result.clear();
    AlbumFields(album, result);
    latest = record.sequence;
  }
  if (latest)
    out = std::move(result);
  return latest.has_value();
}
static std::vector<std::string> ArtistNames(const std::string &text) {
  std::vector<std::string> names;
  for (size_t p = 0; p < text.size();) {
    size_t end = text.find(';', p);
    if (end == std::string::npos)
      end = text.size();
    size_t first = text.find_first_not_of(" \t", p), last = text.find_last_not_of(" \t", end - 1);
    if (first == std::string::npos || first >= end || last < first || names.size() >= 32)
      return {};
    names.push_back(text.substr(first, last - first + 1));
    p = end + 1;
  }
  return names;
}
static bool RicherArtists(const std::string &cached, const std::string &current) {
  const auto proposed = ArtistNames(cached), existing = ArtistNames(current);
  return !existing.empty() && proposed.size() > existing.size() &&
         std::all_of(existing.begin(), existing.end(), [&](const auto &name) {
           return std::find(proposed.begin(), proposed.end(), name) != proposed.end();
         });
}
static void MergeCachedFields(const std::map<std::string, std::string> &cached,
                              std::map<std::string, std::string> &live) {
  // Guest credits can enrich ARTIST. A populated ALBUMARTIST takes precedence.
  for (const auto &[tag, value] : cached) {
    auto &old = live[tag];
    if (old.empty() || (tag == "ARTIST" && RicherArtists(value, old)) ||
        (tag == "DATE" && value.size() > old.size() &&
         Bytes(value).substr(0, 4) == Bytes(old).substr(0, 4)))
      old = value;
  }
}
void MergeCachedAlbum(const CachedRecords &records, CachedTrackMetadata &track) {
  std::map<std::string, std::string> fields;
  if (DecodeCachedAlbum(records, track.album_uri, fields))
    MergeCachedFields(fields, track.fields);
}
bool MergeCachedMetadata(const CachedTrackMetadata &cached, RichMetadata &live) {
  if (cached.uri != live.uri || cached.title != live.title || cached.album != live.album ||
      std::fabs(cached.duration - live.duration) > 1.0)
    return false;

  if (live.album_uri.empty() && ValidSpotifyUri(cached.album_uri, album_prefix))
    live.album_uri = cached.album_uri;
  if (ValidSpotifyUri(live.album_uri, album_prefix))
    live.fields["SPOTIFY_ALBUM_URI"] = live.album_uri;

  MergeCachedFields(cached.fields, live.fields);
  return true;
}
} // namespace history
