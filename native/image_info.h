#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
struct ImageInfo { uint32_t width=0,height=0,depth=0,colors=0; std::string mime; };
// Bounded structural validation; does not decompress pixels.
bool ReadImageInfo(const std::vector<uint8_t>& bytes,ImageInfo& info);
}
