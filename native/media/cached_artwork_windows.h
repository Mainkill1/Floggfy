#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
// Worker-only, bounded read of an exact URL from Spotify's existing disk cache.
// Never opens a cache engine, writes files, or performs a network request.
// On failure, leaves bytes unchanged and returns a fixed diagnostic reason.
bool ReadCachedArtwork(const std::string& url,std::vector<uint8_t>& bytes,std::string& reason);
}
