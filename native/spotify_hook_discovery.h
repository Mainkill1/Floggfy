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
    DecodedOutput,
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
struct DecodedOutputTarget {std::uint32_t rva=0;};
// Optional shared decoder dispatcher. Discovery validates the x64 unwind entry,
// codec virtual call and exact input/output span accounting before hooking it.
DiscoveryResult DiscoverDecodedOutputTarget(const std::uint8_t* mapped_image,
    std::size_t available,DecodedOutputTarget& target) noexcept;
struct DecoderOwnershipLayout {
    std::uint32_t decoded_output=0;
    std::int32_t ogg_capture_to_decoder=0;
    std::int32_t flac_capture_to_decoder=0;
    std::int32_t aac_capture_to_decoder=0;
    bool aac_verified=false;
};
// Stronger optional capability: returns only ownership relationships proven
// against the shared decoder object; unproved codecs remain explicitly disabled.
DiscoveryResult DiscoverDecoderOwnershipLayout(const std::uint8_t* mapped_image,
    std::size_t available,DecoderOwnershipLayout& layout) noexcept;

// Optional Connect observation capability, independent of audio discovery.
// parser: unsigned char (*)(void* json_out,const void* native_string_in).
// Only the two verified return sites carry complete command JSON; another
// call in their owner parses the compressed envelope and must be ignored.
struct ConnectDecodeTargets {
    std::uint32_t parser=0,plain_return=0,decoded_return=0;
};
DiscoveryResult DiscoverConnectDecodeTargets(const std::uint8_t* mapped_image,
    std::size_t available,ConnectDecodeTargets& targets) noexcept;

} // namespace spotify
