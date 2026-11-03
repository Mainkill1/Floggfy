#pragma once
#include "library_layout.h"
#include "playback_quality.h"
#include <cstdint>
#include <string>

namespace history {
struct AutoSkipRequest {
    std::uint32_t token=0;
    unsigned settings_generation=0;
    std::string uri;
};

bool ShouldAutoSkipDownloaded(const PlaybackQualitySnapshot& current,
                              std::uint64_t expected_identity,
                              const Quality& existing) noexcept;
std::uint32_t QueueAutoSkipRequest(const std::string& spotify_uri,unsigned settings_generation);
AutoSkipRequest ReadAutoSkipRequest();
}
