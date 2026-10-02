#include "../native/spotify_hook_discovery.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template<class T>
void Put(std::vector<std::uint8_t>& image, std::size_t offset, T value) {
    assert(offset + sizeof(value) <= image.size());
    std::memcpy(image.data() + offset, &value, sizeof(value));
}

constexpr std::uint8_t ogg[] = {
    0x0f,0xb6,0x47,0x1a,0x8d,0x48,0x1b,0x3b,0xe9,0x0f,0x8c,0x1e,0x01,
    0x00,0x00,0x33,0xd2,0x85,0xc0,0x74,0x20,0x44,0x8b,0x43,0x1c,
};
constexpr std::uint8_t sniff[] = {
    0x48,0x89,0x55,0x88,0x4c,0x8b,0xf9,0xc7,0x44,0x24,0x3c,0x4f,0x67,
    0x67,0x53,0xc7,0x44,0x24,0x40,0x53,0x70,0x41,0x43,0x66,0xc7,0x44,
    0x24,0x38,0x49,0x44,0xc6,0x44,0x24,0x3a,0x33,0xc7,0x44,0x24,0x44,
    0x66,0x74,0x79,0x70,0xc7,0x44,0x24,0x48,0x66,0x4c,0x61,0x43,0xc7,
    0x44,0x24,0x4c,0x52,0x49,0x46,0x46,0xc7,0x44,0x24,0x50,0x57,0x41,
    0x56,0x45,
};
constexpr std::uint8_t flac_init[] = {
    0x48,0x8b,0x57,0x20,0x48,0x89,0x47,0x20,0x48,0x85,0xd2,0x74,0x0a,
    0x48,0x8b,0x47,0x18,0x48,0x8b,0xcf,0xff,
};
constexpr std::uint8_t flac_read[] = {
    0x4c,0x8d,0x41,0x10,0x40,0x38,0x79,0x38,0x74,0x09,0x49,0x8b,0x48,
    0x08,0x49,0x2b,0x08,0xeb,0x1e,0x49,0x8b,0x48,0x20,0x49,0x3b,0x48,
    0x18,0x72,0x06,0x49,0x2b,0x48,0x18,
};
constexpr std::uint8_t flac_frame[] = {
    0x83,0x7a,0x08,0x02,0x75,0x64,0x48,0x39,0x41,0x08,0x74,0x5e,0x49,
    0x39,0x00,0x74,0x59,0x49,0x39,0x40,0x08,0x74,0x53,0x83,0x7a,0x10,
    0x10,0x74,0x06,0x83,0x7a,0x10,0x18,0x75,0x47,
};
constexpr std::uint8_t flac_error[] = {
    0x48,0x8b,0xd9,0x48,0x63,0xfa,0x75,0x2b,0x85,0xd2,0x75,0x27,
};

struct Function {
    std::uint32_t begin;
    std::uint32_t end;
    std::uint32_t anchor_offset;
    const std::uint8_t* anchor;
    std::size_t anchor_size;
};

std::vector<std::uint8_t> MappedImage(std::uint32_t shift = 0,
                                      bool duplicate_ogg = false,
                                      bool inconsistent_layout = false) {
    std::vector<std::uint8_t> image(0x50000);
    Put<std::uint16_t>(image, 0, 0x5a4d);
    Put<std::uint32_t>(image, 0x3c, 0x80);
    Put<std::uint32_t>(image, 0x80, 0x00004550);
    Put<std::uint16_t>(image, 0x84, 0x8664);
    Put<std::uint16_t>(image, 0x86, 2);
    Put<std::uint16_t>(image, 0x94, 240);
    Put<std::uint16_t>(image, 0x98, 0x20b);
    Put<std::uint32_t>(image, 0x98 + 56,
                       static_cast<std::uint32_t>(image.size()));
    Put<std::uint32_t>(image, 0x98 + 108, 16);
    Put<std::uint32_t>(image, 0x98 + 112 + 3 * 8, 0x1000);

    const std::size_t sections = 0x98 + 240;
    Put<std::uint32_t>(image, sections + 8, 0x1000);
    Put<std::uint32_t>(image, sections + 12, 0x1000);
    Put<std::uint32_t>(image, sections + 16, 0x1000);
    Put<std::uint32_t>(image, sections + 36, 0x40000040);
    Put<std::uint32_t>(image, sections + 40 + 8, 0x47000);
    Put<std::uint32_t>(image, sections + 40 + 12, 0x2000);
    Put<std::uint32_t>(image, sections + 40 + 16, 0x47000);
    Put<std::uint32_t>(image, sections + 40 + 36, 0x60000020);

    std::vector<Function> functions = {
        {0x3000 + shift, 0x308e + shift, 17, flac_error, sizeof(flac_error)},
        {0x3550 + shift, 0x3761 + shift, 107, flac_init, sizeof(flac_init)},
        {0x3c60 + shift, 0x3d4e + shift, 112, flac_read, sizeof(flac_read)},
        {0x4000 + shift, 0x40b5 + shift, 48, flac_frame, sizeof(flac_frame)},
        {static_cast<std::uint32_t>((inconsistent_layout ? 0x1a000 : 0x9000) + shift),
         static_cast<std::uint32_t>((inconsistent_layout ? 0x1aaf6 : 0x9af6) + shift),
         55, sniff, sizeof(sniff)},
        {0x2b000 + shift, 0x2b1b4 + shift, 103, ogg, sizeof(ogg)},
    };
    if (duplicate_ogg)
        functions.push_back({0x3b000, 0x3b1b4, 103, ogg, sizeof(ogg)});

    Put<std::uint32_t>(image, 0x98 + 112 + 3 * 8 + 4,
                       static_cast<std::uint32_t>(functions.size() * 12));
    for (std::size_t index = 0; index < functions.size(); ++index) {
        const auto& function = functions[index];
        Put<std::uint32_t>(image, 0x1000 + index * 12, function.begin);
        Put<std::uint32_t>(image, 0x1000 + index * 12 + 4, function.end);
        Put<std::uint32_t>(image, 0x1000 + index * 12 + 8, 0x1800);
        std::memcpy(image.data() + function.begin + function.anchor_offset,
                    function.anchor, function.anchor_size);
    }
    return image;
}

std::uint32_t Rva(const spotify::HookTargets& targets,
                  spotify::TargetKind kind) {
    return spotify::TargetRva(targets, kind);
}

} // namespace

int main() {
    spotify::HookTargets targets{};
    auto image = MappedImage();
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::Found);
    assert(Rva(targets, spotify::TargetKind::FlacError) == 0x3000);
    assert(Rva(targets, spotify::TargetKind::FlacInit) == 0x3550);
    assert(Rva(targets, spotify::TargetKind::FlacRead) == 0x3c60);
    assert(Rva(targets, spotify::TargetKind::FlacFrame) == 0x4000);
    assert(Rva(targets, spotify::TargetKind::FormatSniff) == 0x9000);
    assert(Rva(targets, spotify::TargetKind::OggPageSeek) == 0x2b000);

    image = MappedImage(0x1000);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::Found);
    assert(Rva(targets, spotify::TargetKind::FlacError) == 0x4000);
    assert(Rva(targets, spotify::TargetKind::OggPageSeek) == 0x2c000);

    image = MappedImage();
    image[0x9000 + 55] ^= 0xff;
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::MissingTarget);

    image = MappedImage(0, true);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::AmbiguousTarget);

    image = MappedImage(0, false, true);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) ==
           spotify::DiscoveryResult::InconsistentLayout);

    image = MappedImage();
    Put<std::uint32_t>(image, 0x98 + 240 + 40 + 36, 0x40000040);
    assert(spotify::DiscoverHookTargets(image.data(), image.size(), targets) !=
           spotify::DiscoveryResult::Found);
}
