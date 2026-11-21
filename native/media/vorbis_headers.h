#pragma once
#include <cstdint>
#include <vector>
namespace history {
enum class VorbisHeadersResult { Valid, Invalid, Unsupported };
// Parse identification and setup with the pinned Vorbis implementation. This
// validates supported headers only: it does not decode audio packets. Bounded
// resource/unsupported-format failures never count as validated headers.
VorbisHeadersResult ValidateVorbisHeaders(const std::vector<uint8_t>& identification,
                                         const std::vector<uint8_t>& setup) noexcept;
}
