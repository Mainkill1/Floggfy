#pragma once
#include <string_view>

namespace history {
inline constexpr std::string_view kArtworkOrigin = "https://i.scdn.co";
inline constexpr std::string_view kArtworkPrefix = "https://i.scdn.co/image/";

inline bool IsAlbumArtworkUrl(std::string_view url) noexcept {
    if (url.size() != kArtworkPrefix.size() + 40 ||
        url.substr(0, kArtworkPrefix.size()) != kArtworkPrefix)
        return false;
    for (char digit : url.substr(kArtworkPrefix.size()))
        if (!(digit >= '0' && digit <= '9') && !(digit >= 'a' && digit <= 'f'))
            return false;
    return true;
}
}
