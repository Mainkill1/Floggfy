// Read-only LevelDB table/WAL and Spotify protobuf decoding. Schema reference:
// https://github.com/librespot-org/librespot/blob/dev/protocol/proto/metadata.proto
// Field numbers are checked alongside Any type and exact embedded GIDs.
#include "cached_metadata.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
namespace history {
namespace {
using Bytes = std::string_view;
constexpr size_t max_file = 64 * 1024 * 1024, max_block = 8 * 1024 * 1024, max_value = 131072;
uint64_t Little(Bytes b) {
  uint64_t n = 0;
  for (size_t i = 0; i < b.size(); ++i)
    n |= uint64_t(uint8_t(b[i])) << (i * 8);
  return n;
}
bool Varint(Bytes b, size_t &p, uint64_t &n) {
  n = 0;
  for (unsigned i = 0; i < 10 && p < b.size(); ++i) {
    uint8_t c = uint8_t(b[p++]);
    if (i == 9 && c > 1)
      return false;
    n |= uint64_t(c & 127) << (7 * i);
    if (!(c & 128))
      return true;
  }
  return false;
}
bool Sized(Bytes b, size_t &p, Bytes &out) {
  uint64_t n;
  if (!Varint(b, p, n) || n > b.size() - p)
    return false;
  out = b.substr(p, size_t(n));
  p += size_t(n);
  return true;
}
constexpr std::array<uint32_t, 256> CrcTable() {
  std::array<uint32_t, 256> a{};
  for (unsigned i = 0; i < a.size(); ++i) {
    uint32_t c = i;
    for (unsigned j = 0; j < 8; ++j)
      c = (c >> 1) ^ ((c & 1) ? 0x82f63b78u : 0);
    a[i] = c;
  }
  return a;
}
constexpr auto crc_table = CrcTable();
uint32_t CrcUpdate(uint32_t c, Bytes b) {
  for (unsigned char n : b)
    c = crc_table[(c ^ n) & 255] ^ (c >> 8);
  return c;
}
uint32_t Mask(uint32_t c) {
  c ^= 0xffffffffu;
  return ((c >> 15) | (c << 17)) + 0xa282ead8u;
}
bool Snappy(Bytes b, std::string &out) {
  size_t p = 0;
  uint64_t expected;
  if (!Varint(b, p, expected) || expected > max_block)
    return false;

  out.clear();
  out.reserve(size_t(expected));
  while (p < b.size()) {
    uint8_t tag = uint8_t(b[p++]);
    unsigned kind = tag & 3;
    uint64_t n = 0, offset = 0;
    if (!kind) {
      n = tag >> 2;
      if (n >= 60) {
        size_t count = size_t(n - 59);
        if (count > b.size() - p)
          return false;
        n = Little(b.substr(p, count));
        p += count;
      }
      ++n;
      if (n > b.size() - p || n > expected - out.size())
        return false;
      out.append(b.data() + p, size_t(n));
      p += size_t(n);
    } else {
      if (kind == 1) {
        if (p == b.size())
          return false;
        n = 4 + ((tag >> 2) & 7);
        offset = ((tag & 224) << 3) | uint8_t(b[p++]);
      } else {
        size_t count = kind == 2 ? 2 : 4;
        if (count > b.size() - p)
          return false;
        n = 1 + (tag >> 2);
        offset = Little(b.substr(p, count));
        p += count;
      }
      if (!offset || offset > out.size() || n > expected - out.size())
        return false;

      for (uint64_t i = 0; i < n; ++i)
        out.push_back(out[out.size() - size_t(offset)]);
    }
  }
  return out.size() == expected;
}
bool Handle(Bytes b, size_t &p, uint64_t &offset, uint64_t &size) {
  return Varint(b, p, offset) && Varint(b, p, size);
}
bool Block(Bytes file, uint64_t offset, uint64_t size, std::string &out) {
  if (offset > file.size() || size > max_block || file.size() - size_t(offset) < 5 ||
      size > file.size() - size_t(offset) - 5)
    return false;

  auto b = file.substr(size_t(offset), size_t(size) + 1);
  uint32_t stored = uint32_t(Little(file.substr(size_t(offset + size + 1), 4)));
  if (Mask(CrcUpdate(0xffffffffu, b)) != stored)
    return false;

  uint8_t kind = uint8_t(b.back());
  b.remove_suffix(1);
  if (kind == 0) {
    out.assign(b);
    return true;
  }
  return kind == 1 && Snappy(b, out);
}
template <class F> bool Entries(Bytes b, F visit) {
  if (b.size() < 4)
    return false;
  size_t restarts = size_t(Little(b.substr(b.size() - 4)));
  if (!restarts || restarts > (b.size() - 4) / 4)
    return false;

  size_t end = b.size() - 4 - restarts * 4, p = 0;
  std::string key;
  unsigned count = 0;
  b = b.substr(0, end);
  while (p < end) {
    uint64_t shared, extra, n;
    if (++count > 131072 || !Varint(b, p, shared) || !Varint(b, p, extra) || !Varint(b, p, n) ||
        shared > key.size() || extra > b.size() - p || extra + shared > 16384)
      return false;

    key.resize(size_t(shared));
    key.append(b.data() + p, size_t(extra));
    p += size_t(extra);
    if (n > b.size() - p)
      return false;
    auto value = b.substr(p, size_t(n));
    p += size_t(n);
    if (!visit(Bytes(key), value))
      return false;
  }
  return p == end;
}
bool Wanted(Bytes key, const std::string &uri) {
  constexpr Bytes prefix = "!xmeta#cache#";
  if (key.substr(0, prefix.size()) != prefix)
    return false;

  const auto suffix = "#$" + uri + "#";
  return key.size() >= suffix.size() && key.substr(key.size() - suffix.size()) == suffix;
}
bool Record(CachedRecords &out, Bytes key, Bytes value, uint64_t sequence, bool deleted,
            const std::string &uri) {
  if (!Wanted(key, uri))
    return true;
  if (key.size() > 16384 || value.size() > max_value ||
      (!out.count(std::string(key)) && out.size() >= 32))
    return false;

  auto &old = out[std::string(key)];
  if (sequence >= old.sequence)
    old = {sequence, deleted, std::string(value)};
  return true;
}
bool Batch(Bytes b, const std::string &uri, CachedRecords &out) {
  if (b.size() < 12)
    return false;
  uint64_t sequence = Little(b.substr(0, 8)), count = Little(b.substr(8, 4));
  size_t p = 12;
  if (count > 65536 || sequence > ((uint64_t(1) << 56) - 1) - count)
    return false;

  for (uint64_t i = 0; i < count; ++i) {
    if (p == b.size())
      return false;
    uint8_t kind = uint8_t(b[p++]);
    Bytes key, value;
    if (kind > 1 || !Sized(b, p, key) || (kind == 1 && !Sized(b, p, value)) ||
        !Record(out, key, value, sequence + i, kind == 0, uri))
      return false;
  }
  return p == b.size();
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
bool CollectMetadataTable(const std::vector<uint8_t> &bytes, const std::string &uri,
                          CachedRecords &out) {
  if (bytes.size() < 48 || bytes.size() > max_file)
    return false;
  Bytes file(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  if (Little(file.substr(file.size() - 8)) != 0xdb4775248b80fb57ULL)
    return false;

  auto footer = file.substr(file.size() - 48, 40);
  size_t p = 0;
  uint64_t offset, size;
  if (!Handle(footer, p, offset, size) || !Handle(footer, p, offset, size))
    return false;

  std::string index;
  if (!Block(file, offset, size, index))
    return false;
  auto staged = out;
  size_t blocks = 0, expanded = index.size();
  uint64_t previous_end = 0;
  constexpr size_t max_expanded = 32 * 1024 * 1024;
  bool valid = Entries(index, [&](Bytes, Bytes handle) {
    size_t cursor = 0;
    uint64_t start, n;
    std::string data;
    if (++blocks > 4096 || !Handle(handle, cursor, start, n) || cursor != handle.size() ||
        start < previous_end || start > offset || offset - start < 5 || n > offset - start - 5 ||
        !Block(file, start, n, data) || data.size() > max_expanded - expanded)
      return false;

    previous_end = start + n + 5;
    expanded += data.size();
    return Entries(data, [&](Bytes key, Bytes value) {
      if (key.size() < 8)
        return false;
      uint64_t trailer = Little(key.substr(key.size() - 8));
      unsigned kind = unsigned(trailer & 255);
      key.remove_suffix(8);
      if (kind > 1)
        return false;
      return Record(staged, key, value, trailer >> 8, kind == 0, uri);
    });
  });
  if (valid)
    out = std::move(staged);
  return valid;
}
bool CollectMetadataLog(const std::vector<uint8_t> &bytes, const std::string &uri,
                        CachedRecords &out) {
  if (bytes.size() > max_file)
    return false;
  Bytes file(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  size_t p = 0;
  std::string assembling;
  bool first = false, complete = false;
  auto staged = out;
  while (p < file.size()) {
    size_t available = 32768 - p % 32768;
    if (available < 7) {
      p += std::min(available, file.size() - p);
      continue;
    }
    if (file.size() - p < 7)
      break;
    auto header = file.substr(p, 7);
    size_t n = size_t(Little(header.substr(4, 2)));
    uint8_t kind = uint8_t(header[6]);
    if (!kind && !n) {
      p += std::min(available, file.size() - p);
      continue;
    }
    if (n > available - 7)
      return false;
    if (n > file.size() - p - 7)
      break;
    auto payload = file.substr(p + 7, n);
    p += 7 + n;
    if (Mask(CrcUpdate(CrcUpdate(0xffffffffu, header.substr(6, 1)), payload)) !=
        Little(header.substr(0, 4)))
      return false;

    if (kind == 1) {
      if (!Batch(payload, uri, staged))
        return false;
      complete = true;
      first = false;
      assembling.clear();
    } else if (kind == 2) {
      assembling.assign(payload);
      first = true;
    } else if (kind == 3 || kind == 4) {
      if (!first || payload.size() > max_value - std::min(max_value, assembling.size()))
        return false;
      assembling.append(payload);
      if (kind == 4) {
        if (!Batch(assembling, uri, staged))
          return false;
        complete = true;
        first = false;
        assembling.clear();
      }
    } else
      return false;
  }
  if (complete)
    out = std::move(staged);
  return complete;
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

  for (const auto &field : cached.fields) {
    auto old = live.fields.find(field.first);
    bool replace = old == live.fields.end() || old->second.empty();
    if (!replace && (field.first == "ARTIST" || field.first == "ALBUMARTIST"))
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
