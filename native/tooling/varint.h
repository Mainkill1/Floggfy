#pragma once
#include <cstdint>
#include <string_view>

namespace wire {
inline bool Varint(std::string_view b, size_t &p, uint64_t &n) {
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
inline bool Sized(std::string_view b, size_t &p, std::string_view &out) {
  uint64_t n;
  if (!Varint(b, p, n) || n > b.size() - p)
    return false;
  out = b.substr(p, size_t(n));
  p += size_t(n);
  return true;
}
}
