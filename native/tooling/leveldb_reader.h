#pragma once
#include "varint.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace leveldb {
namespace detail {
using Bytes = std::string_view;
using wire::Varint;
using wire::Sized;
constexpr size_t max_file = 64 * 1024 * 1024, max_block = 8 * 1024 * 1024, max_log_record = 131072;
inline uint64_t Little(Bytes b) {
  uint64_t n = 0;
  for (size_t i = 0; i < b.size(); ++i)
    n |= uint64_t(uint8_t(b[i])) << (i * 8);
  return n;
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
inline constexpr auto crc_table = CrcTable();
inline uint32_t CrcUpdate(uint32_t c, Bytes b) {
  for (unsigned char n : b)
    c = crc_table[(c ^ n) & 255] ^ (c >> 8);
  return c;
}
inline uint32_t Mask(uint32_t c) {
  c ^= 0xffffffffu;
  return ((c >> 15) | (c << 17)) + 0xa282ead8u;
}
inline bool Snappy(Bytes b, std::string &out) {
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
inline bool Handle(Bytes b, size_t &p, uint64_t &offset, uint64_t &size) {
  return Varint(b, p, offset) && Varint(b, p, size);
}
inline bool Block(Bytes file, uint64_t offset, uint64_t size, std::string &out) {
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
template<class Visitor> bool Batch(Bytes b, const Visitor& visit) {
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
        !visit(key, value, sequence + i, kind == 0))
      return false;
  }
  return p == b.size();
}
}

// Visit borrowed key/value bytes with their sequence and deletion flag.
// A later block can invalidate the file. Commit collected records only on success.
template<class Visitor> bool VisitTable(const std::vector<uint8_t>& bytes, const Visitor& visit) {
  using namespace detail;
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
  size_t blocks = 0, expanded = index.size();
  uint64_t previous_end = 0;
  constexpr size_t max_expanded = 32 * 1024 * 1024;
  return Entries(index, [&](Bytes, Bytes handle) {
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
      return visit(key, value, trailer >> 8, kind == 0);
    });
  });
}
template<class Visitor> bool VisitLog(const std::vector<uint8_t>& bytes, const Visitor& visit) {
  using namespace detail;
  if (bytes.size() > max_file)
    return false;
  Bytes file(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  size_t p = 0;
  std::string assembling;
  bool first = false, complete = false;
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
      if (!Batch(payload, visit))
        return false;
      complete = true;
      first = false;
      assembling.clear();
    } else if (kind == 2) {
      assembling.assign(payload);
      first = true;
    } else if (kind == 3 || kind == 4) {
      if (!first || payload.size() > max_log_record - std::min(max_log_record, assembling.size()))
        return false;
      assembling.append(payload);
      if (kind == 4) {
        if (!Batch(assembling, visit))
          return false;
        complete = true;
        first = false;
        assembling.clear();
      }
    } else
      return false;
  }
  return complete;
}
}
