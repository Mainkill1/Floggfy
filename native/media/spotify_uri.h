#pragma once
#include <string_view>

namespace history {
inline bool ValidSpotifyUri(std::string_view uri,std::string_view prefix) noexcept {
    if(uri.size()!=prefix.size()+22 || uri.compare(0,prefix.size(),prefix)!=0)return false;
    for(char c:uri.substr(prefix.size()))
        if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')))return false;
    return true;
}
inline bool ValidSpotifyTrackUri(std::string_view uri) noexcept {
    return ValidSpotifyUri(uri,"spotify:track:");
}
inline bool ValidSpotifyMediaUri(std::string_view uri) noexcept {
    return ValidSpotifyTrackUri(uri)||ValidSpotifyUri(uri,"spotify:episode:");
}
}
