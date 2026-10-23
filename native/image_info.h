#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
struct ImageInfo { uint32_t width=0,height=0,depth=0,colors=0; std::string mime; };
// Bounded structural validation; does not decompress pixels.
bool ReadImageInfo(const std::vector<uint8_t>& bytes,ImageInfo& info);
struct ThumbnailRefresh {
    std::string key; uint64_t attempted=0; bool initialized=false;
    bool Due(const std::string& next,uint64_t now) {
        if(initialized && next==key && now-attempted<30000) return false;
        key=next;attempted=now;initialized=true;return true;
    }
};
}
