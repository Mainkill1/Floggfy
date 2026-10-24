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
// Completed identity is returned only after continuous coverage and a natural
// transition. Same-title repeats have distinct generations. A provisional
// repeat emits the prior completion once; its new generation may be relabeled
// once within the original two-second title window without resetting coverage.
// Expiring that label window does not invalidate a genuine repeat's coverage.
enum class ListenPhase { Tracking, AwaitingIdentity, ProvisionalRepeat };
struct Listen {
    std::string identity;
    double start_time = 0, last_time = 0, last_position = 0, duration = 0;
    double identity_time = 0;
    std::uint64_t generation = 0, completed_generation = 0;
    double completed_start = 0, completed_time = 0;
    ListenPhase phase = ListenPhase::Tracking;
    // These describe the observed wrap. They are active only in AwaitingIdentity
    // or ProvisionalRepeat; confirmation must never extend wrap_time's deadline.
    double wrap_time = 0, wrap_start = 0, wrap_position = 0, wrap_length = 0;
    bool eligible = false, playing = false, transient = false, pending_start = false;
    std::string Observe(const std::string& key, double position, double length,
                        bool is_playing, double time);
};
}
