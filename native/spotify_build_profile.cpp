#include "spotify_build_profile.h"

#include <algorithm>
#include <cstring>

namespace spotify {
namespace {

constexpr HookTarget Target(TargetKind kind, std::uint32_t rva,
                            std::array<std::uint8_t, 16> signature) {
    return HookTarget{kind, rva, signature, 16};
}

constexpr BuildProfile Profile(const char (&sha256)[65],std::array<HookTarget,6> targets) {
    BuildProfile profile{};
    for(std::size_t i=0;i<65;++i) profile.sha256[i]=sha256[i];
    profile.targets=targets;
    return profile;
}

constexpr auto ogg_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
constexpr auto sniff_signature=std::array<std::uint8_t,16>{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55};
constexpr auto flac_init_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x54,0x41,0x56};
constexpr auto flac_read_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41};
constexpr auto flac_frame_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41};
constexpr auto flac_error_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x80,0xb9,0xb0,0x00,0x00,0x00};
constexpr auto flac_error_1292_signature=std::array<std::uint8_t,16>{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x80,0xb9,0xa8,0x00,0x00,0x00};

constexpr BuildProfile profiles[] = {
    Profile("0731eca3ec438395815907c04653c63a917b55bf0ebdd83c96f041424a92b54b",{{
        Target(TargetKind::OggPageSeek, 0xebef18,
               ogg_signature),
        Target(TargetKind::FormatSniff, 0xe9c550,
               sniff_signature),
        Target(TargetKind::FlacInit, 0xe95f2c,
               flac_init_signature),
        Target(TargetKind::FlacRead, 0xe9663c,
               flac_read_signature),
        Target(TargetKind::FlacFrame, 0xe969f4,
               flac_frame_signature),
        Target(TargetKind::FlacError, 0xe959d0,
               flac_error_signature),
    }}),
    Profile("65c131dc7f9dcce90eb3874033e6e22ef422a493f0877e3f031805198839b7c4",{{
        Target(TargetKind::OggPageSeek,0xe9d168,ogg_signature),
        Target(TargetKind::FormatSniff,0xe7a7a0,sniff_signature),
        Target(TargetKind::FlacInit,0xe7417c,flac_init_signature),
        Target(TargetKind::FlacRead,0xe7488c,flac_read_signature),
        Target(TargetKind::FlacFrame,0xe74c44,flac_frame_signature),
        Target(TargetKind::FlacError,0xe73c20,flac_error_signature),
    }}),
    Profile("0c827576c4394fa2cc7419867d147546a7ea82e52c577ddb9063a95806ea9b11",{{
        Target(TargetKind::OggPageSeek,0xe3e288,ogg_signature),
        Target(TargetKind::FormatSniff,0xe1b85c,sniff_signature),
        Target(TargetKind::FlacInit,0xe1586c,flac_init_signature),
        Target(TargetKind::FlacRead,0xe15f7c,flac_read_signature),
        Target(TargetKind::FlacFrame,0xe16334,flac_frame_signature),
        Target(TargetKind::FlacError,0xe15310,flac_error_signature),
    }}),
    Profile("7b44456a90142daeb758e2736d8628ffe211d6ea523db8f1050b3c8b1addb68a",{{
        Target(TargetKind::OggPageSeek,0xdd94e8,ogg_signature),
        Target(TargetKind::FormatSniff,0xdb68a4,sniff_signature),
        Target(TargetKind::FlacInit,0xdb150c,flac_init_signature),
        Target(TargetKind::FlacRead,0xdb1c1c,flac_read_signature),
        Target(TargetKind::FlacFrame,0xdb1f50,flac_frame_signature),
        Target(TargetKind::FlacError,0xdb0fbc,flac_error_1292_signature),
    }}),
};

template<class T>
bool Read(const std::uint8_t* image, std::size_t size, std::size_t offset, T& value) noexcept {
    if (offset > size || sizeof(T) > size - offset) return false;
    std::memcpy(&value, image + offset, sizeof(T));
    return true;
}

bool Range(std::size_t size, std::size_t offset, std::size_t length) noexcept {
    return offset <= size && length <= size - offset;
}

bool InExecutableSection(const std::uint8_t* image, std::size_t size,
                         std::size_t sections, std::uint16_t count,
                         const HookTarget& target) noexcept {
    constexpr std::uint32_t executable = 0x20000000;
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t section = sections + std::size_t{index} * 40;
        std::uint32_t virtual_size = 0, virtual_address = 0, raw_size = 0, flags = 0;
        if (!Read(image, size, section + 8, virtual_size) ||
            !Read(image, size, section + 12, virtual_address) ||
            !Read(image, size, section + 16, raw_size) ||
            !Read(image, size, section + 36, flags)) return false;
        const std::uint64_t extent = std::max(virtual_size, raw_size);
        const std::uint64_t begin = virtual_address;
        const std::uint64_t end = begin + extent;
        const std::uint64_t target_end = std::uint64_t{target.rva} + target.signature_size;
        if ((flags & executable) && target.rva >= begin && target_end <= end) return true;
    }
    return false;
}

} // namespace

const BuildProfile* FindBuildProfile(const char* sha256) noexcept {
    if (!sha256) return nullptr;
    for (const auto& profile : profiles) {
        if (std::strcmp(profile.sha256.data(), sha256) == 0) return &profile;
    }
    return nullptr;
}

bool ValidateMappedProfile(const std::uint8_t* image, std::size_t available,
                           const BuildProfile& profile) noexcept {
    if (!image || available < 0x40) return false;
    std::uint16_t dos = 0, machine = 0, section_count = 0, optional_size = 0, magic = 0;
    std::uint32_t nt = 0, signature = 0, image_size = 0;
    if (!Read(image, available, 0, dos) || dos != 0x5a4d ||
        !Read(image, available, 0x3c, nt) || nt > 0x100000 ||
        !Read(image, available, nt, signature) || signature != 0x00004550 ||
        !Read(image, available, nt + 4, machine) || machine != 0x8664 ||
        !Read(image, available, nt + 6, section_count) || !section_count ||
        !Read(image, available, nt + 20, optional_size) || optional_size < 240 ||
        !Read(image, available, nt + 24, magic) || magic != 0x20b ||
        !Read(image, available, nt + 24 + 56, image_size) || image_size > available)
        return false;
    const std::size_t sections = std::size_t{nt} + 24 + optional_size;
    if (!Range(available, sections, std::size_t{section_count} * 40)) return false;
    for (const auto& target : profile.targets) {
        if (!target.rva || !target.signature_size ||
            target.signature_size > target.signature.size() ||
            !Range(image_size, target.rva, target.signature_size) ||
            !InExecutableSection(image, available, sections, section_count, target) ||
            std::memcmp(image + target.rva, target.signature.data(), target.signature_size) != 0)
            return false;
    }
    return true;
}

} // namespace spotify
