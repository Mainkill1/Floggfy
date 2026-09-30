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
};

struct HookTarget {
    TargetKind kind{};
    std::uint32_t rva = 0;
    std::array<std::uint8_t, 16> signature{};
    std::uint8_t signature_size = 0;
};

struct BuildProfile {
    std::array<char, 65> sha256{};
    std::array<HookTarget, 6> targets{};
};

const BuildProfile* FindBuildProfile(const char* sha256) noexcept;
bool ValidateMappedProfile(const std::uint8_t* image, std::size_t available,
                           const BuildProfile& profile) noexcept;

} // namespace spotify
