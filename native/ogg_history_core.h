#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace history {
struct Page {
    uint32_t serial = 0, sequence = 0, rate = 0, bitrate = 0;
    int64_t granule = -1;
    uint8_t flags = 0, channels = 0;
    bool vorbis_start = false;
};
bool ParsePage(const uint8_t* bytes, size_t length, Page& out);
enum class Result { Ignore, Begin, Append, Complete, Invalid };
struct Stream {
    bool active = false, complete = false;
    uint32_t serial = 0, next = 0, rate = 0, pages = 0, bitrate = 0;
    uint8_t channels = 0;
    int64_t samples = -1;
    Result Push(const uint8_t* bytes, size_t length);
    double Duration() const { return rate && samples > 0 ? double(samples) / rate : 0; }
};
// Media positions must be extrapolated from the public timeline timestamp.
// Completed identity is returned only on a natural transition at the end.
struct Listen {
    std::string identity;
    double start_time = 0, last_time = 0, last_position = 0, duration = 0;
    double identity_time = 0;
    bool eligible = false, playing = false, transient = false, pending_start = false;
    std::string Observe(const std::string& key, double position, double length,
                        bool is_playing, double time);
};
}
