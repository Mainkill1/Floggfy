#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace history {
// Preserve validated JPEG/PNG or convert a bounded still WebP to PNG.
// On failure the original bytes remain unchanged.
bool NormalizeArtworkImage(std::vector<uint8_t>& bytes,std::string& reason);
}
