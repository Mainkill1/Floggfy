#pragma once
#include <cstddef>
#include <cstdint>

namespace history {
struct Page {
    uint32_t serial = 0, sequence = 0, rate = 0, bitrate = 0;
    int64_t granule = -1;
    uint8_t flags = 0, channels = 0;
    bool vorbis_start = false;
};
bool ParsePage(const uint8_t* bytes, size_t length, Page& out);
// Extract a structurally bounded serial/sequence before CRC validation. This is
// used only to quarantine the exact failed logical stream; it never accepts or
// publishes an invalid page.
bool ParsePageIdentity(const uint8_t* bytes,size_t length,uint32_t& serial,uint32_t& sequence);
enum class Result { Ignore, Begin, Append, Complete, Invalid };
enum class OggRejection : uint8_t {
    None,InvalidPage,Inactive,Serial,Sequence,Bos,Granule,
    GranuleRegression,EosGranule
};
const char* OggRejectionName(OggRejection reason) noexcept;
struct Stream {
    bool active = false, complete = false;
    OggRejection rejection = OggRejection::None;
    uint32_t serial = 0, next = 0, rate = 0, pages = 0, bitrate = 0;
    uint8_t channels = 0;
    int64_t samples = -1;
    Result Push(const uint8_t* bytes, size_t length);
    double Duration() const { return rate && samples > 0 ? double(samples) / rate : 0; }
};
}
