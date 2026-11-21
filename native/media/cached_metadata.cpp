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
  if (b.empty() || b.size() > 4096)
    return {};
  std::string s(b);
  return MetadataTextValid(s) ? s : std::string{};
}
constexpr char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
bool Id(const std::string &uri, const char *kind, std::array<uint8_t, 16> &gid) {
  std::string prefix = std::string("spotify:") + kind + ":";
  if (uri.size() != prefix.size() + 22 || uri.compare(0, prefix.size(), prefix) != 0)
    return false;

  gid.fill(0);
  for (char c : uri.substr(prefix.size())) {
    const char *found = std::find(alphabet, alphabet + 62, c);
    if (found == alphabet + 62)
      return false;
    unsigned carry = unsigned(found - alphabet);
    for (int i = 15; i >= 0; --i) {
      carry += unsigned(gid[size_t(i)]) * 62;
      gid[size_t(i)] = uint8_t(carry);
      carry >>= 8;
    }
    if (carry)
      return false;
  }
  return true;
}
bool SameId(Bytes value, const std::string &uri, const char *kind) {
  std::array<uint8_t, 16> gid{};
  return Id(uri, kind, gid) && value.size() == gid.size() &&
         std::equal(gid.begin(), gid.end(), reinterpret_cast<const uint8_t *>(value.data()));
}
std::string AlbumUri(Bytes gid) {
  if (gid.size() != 16)
    return {};
  std::array<uint8_t, 16> n{};
  std::copy(gid.begin(), gid.end(), n.begin());
  std::string id(22, '0');
  for (int i = 21; i >= 0; --i) {
    unsigned remainder = 0;
    for (auto &byte : n) {
      unsigned v = (remainder << 8) | byte;
      byte = uint8_t(v / 62);
      remainder = v % 62;
    }
    id[size_t(i)] = alphabet[remainder];
  }
  return "spotify:album:" + id;
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
std::vector<std::string> Artists(const Message &m, unsigned key) {
  std::vector<std::string> names;
  for (const auto &f : m)
    if (f.key == key && f.wire == 2) {
      Message artist;
      if (Proto(f.bytes, artist))
        Add(names, Text(artist, 2));
    }
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
  for (const auto &f : album)
    if (f.key == 13 && f.wire == 2) {
      Message notice;
      if (Proto(f.bytes, notice)) {
        auto text = Text(notice, 2);
        if (Number(notice, 1) == 1)
          Add(copyright, text);
        else if (Number(notice, 1) == 0)
          Add(phonogram, text);
      }
    }
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
  uint64_t latest = 0;
  bool found = false;
  CachedTrackMetadata result;
  for (const auto &entry : records) {
    const auto &r = entry.second;
    Message track, album;
    if (!Payload(r, "spotify.metadata.Track", track) || !SameId(Data(track, 1), uri, "track") ||
        !Proto(Data(track, 3), album))
      continue;
    CachedTrackMetadata m;
    m.uri = uri;
    m.title = Text(track, 2);
    m.album = Text(album, 2);
    m.album_uri = AlbumUri(Data(album, 1));
    m.duration = double(Signed(track, 7)) / 1000;
    if (m.title.empty() || m.album.empty() || m.album_uri.empty() || !std::isfinite(m.duration) ||
        m.duration <= 0 || m.duration > 86400)
      continue;
    Put(m.fields, "ARTIST", Join(Artists(track, 4)));
    AlbumFields(album, m.fields);
    for (const auto &f : track)
      if (f.key == 10 && f.wire == 2) {
        Message external;
        if (Proto(f.bytes, external) && Text(external, 1) == "isrc")
          Put(m.fields, "ISRC", Text(external, 2));
      }
    auto number = Signed(track, 5), disc = Signed(track, 6);
    if (number > 0 && number < 100000)
      Put(m.fields, "TRACKNUMBER", std::to_string(number));
    if (disc > 0 && disc < 100000)
      Put(m.fields, "DISCNUMBER", std::to_string(disc));
    std::vector<std::string> composer, remixer, conductor;
    for (const auto &f : track)
      if (f.key == 32 && f.wire == 2) {
        Message role;
        if (Proto(f.bytes, role)) {
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
          default:
            break;
          }
        }
      }
    Put(m.fields, "COMPOSER", Join(composer));
    Put(m.fields, "REMIXER", Join(remixer));
    Put(m.fields, "CONDUCTOR", Join(conductor));
    if (!found || r.sequence >= latest) {
      latest = r.sequence;
      result = std::move(m);
      found = true;
    }
  }
  if (found)
    out = std::move(result);
  return found;
}
bool DecodeCachedAlbum(const CachedRecords &records, const std::string &uri,
                       std::map<std::string, std::string> &out) {
  uint64_t latest = 0;
  bool found = false;
  std::map<std::string, std::string> result;
  for (const auto &entry : records) {
    Message album;
    const auto &r = entry.second;
    if (Payload(r, "spotify.metadata.Album", album) && SameId(Data(album, 1), uri, "album") &&
        (!found || r.sequence >= latest)) {
      result.clear();
      AlbumFields(album, result);
      found = true;
      latest = r.sequence;
    }
  }
  if (found)
    out = std::move(result);
  return found;
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
bool MergeCachedMetadata(const CachedTrackMetadata &cached, RichMetadata &live) {
  if (cached.uri != live.uri || cached.title != live.title || cached.album != live.album ||
      std::fabs(cached.duration - live.duration) > 1.0)
    return false;

  if (live.album_uri.empty() && ValidSpotifyUri(cached.album_uri,"spotify:album:"))
    live.album_uri = cached.album_uri;
  if (ValidSpotifyUri(live.album_uri,"spotify:album:"))
    live.fields["SPOTIFY_ALBUM_URI"] = live.album_uri;

  for (const auto &field : cached.fields) {
    auto old = live.fields.find(field.first);
    bool replace = old == live.fields.end() || old->second.empty();
    // Guest credits can enrich ARTIST, but a longer album-artist list must not
    // replace a populated current album artist.
    if (!replace && field.first == "ARTIST")
      replace = RicherArtists(field.second, old->second);
    if (!replace && field.first == "DATE")
      replace = old->second.substr(0, 4) == field.second.substr(0, 4) &&
                field.second.size() > old->second.size();
    if (replace)
      live.fields[field.first] = field.second;
  }
  return true;
}
} // namespace history
