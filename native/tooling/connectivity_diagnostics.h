#pragma once
#include <atomic>
#include <cstdint>

namespace history {
// Last result of an EXISTING NLM query, never an extra network probe. Flags
// describe Windows connectivity reporting, not Spotify/CDN reachability. A
// contended read/update is omitted instead of waiting in either hook/worker.
struct ConnectivityObservation {
    bool known=false,flags_valid=false;
    std::int32_t result=0;
    std::uint32_t original=0,effective=0;
    std::uint64_t observed_ms=0;
};
inline std::atomic_flag connectivity_observation_lock=ATOMIC_FLAG_INIT;
inline ConnectivityObservation connectivity_observation;
inline void RecordConnectivity(const ConnectivityObservation& value) noexcept {
    if(connectivity_observation_lock.test_and_set(std::memory_order_acquire))return;
    connectivity_observation=value;
    connectivity_observation_lock.clear(std::memory_order_release);
}
inline ConnectivityObservation LastConnectivity() noexcept {
    if(connectivity_observation_lock.test_and_set(std::memory_order_acquire))return {};
    const auto value=connectivity_observation;
    connectivity_observation_lock.clear(std::memory_order_release);
    return value;
}
}
