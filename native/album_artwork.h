#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
bool IsAlbumArtworkUrl(const std::string& url);
// Online fallback only after the caller has attempted local artwork retrieval.
bool DownloadAlbumArtwork(const std::string& url,std::vector<uint8_t>& bytes);
}
