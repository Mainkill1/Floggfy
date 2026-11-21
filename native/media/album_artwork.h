#pragma once
#include "artwork_url.h"
#include <cstdint>
#include <string>
#include <vector>
namespace history {
// Online fallback only after the caller has attempted local artwork retrieval.
bool DownloadAlbumArtwork(const std::string& url,std::vector<uint8_t>& bytes);
}
