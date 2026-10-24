#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace spotify {

enum class TargetKind : unsigned char {
    OggPageSeek,
    FormatSniff,
    FlacInit,
    FlacRead,
    FlacFrame,
    FlacError,
    AacInitialize,
    AacPush,
};

struct HookTarget {
    TargetKind kind{};
    std::uint32_t rva = 0;
};

struct HookTargets {
    std::array<HookTarget, 6> entries{};
};

enum class DiscoveryResult : unsigned char {
    Found,
    InvalidImage,
    MissingTarget,
    AmbiguousTarget,
    InconsistentLayout,
};

DiscoveryResult DiscoverHookTargets(const std::uint8_t* mapped_image,
                                    std::size_t available,
                                    HookTargets& targets) noexcept;
std::uint32_t TargetRva(const HookTargets& targets, TargetKind kind) noexcept;
struct AacHookTargets {std::uint32_t initialize=0,push=0,destroy=0;};
// Optional: a missing AAC capability never disables existing FLAC/Ogg capture.
DiscoveryResult DiscoverAacHookTargets(const std::uint8_t* mapped_image,
    std::size_t available,AacHookTargets& targets) noexcept;

} // namespace spotify
