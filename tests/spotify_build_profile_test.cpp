#include "../native/spotify_build_profile.h"

#include <array>
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

std::vector<std::uint8_t> MappedImage(const spotify::BuildProfile& profile) {
    std::vector<std::uint8_t> image(4096);
    Put<std::uint16_t>(image, 0, 0x5a4d);
    Put<std::uint32_t>(image, 0x3c, 0x80);
    Put<std::uint32_t>(image, 0x80, 0x00004550);
    Put<std::uint16_t>(image, 0x84, 0x8664);
    Put<std::uint16_t>(image, 0x86, 1);
    Put<std::uint16_t>(image, 0x94, 240);
    Put<std::uint16_t>(image, 0x98, 0x20b);
    Put<std::uint32_t>(image, 0x98 + 56, 4096);
    const std::size_t section = 0x98 + 240;
    Put<std::uint32_t>(image, section + 8, 0x600);
    Put<std::uint32_t>(image, section + 12, 0x200);
    Put<std::uint32_t>(image, section + 16, 0x600);
    Put<std::uint32_t>(image, section + 36, 0x60000020);
    for (const auto& target : profile.targets)
        std::memcpy(image.data() + target.rva, target.signature.data(), target.signature_size);
    return image;
}

} // namespace

int main() {
    constexpr const char* known =
        "0731eca3ec438395815907c04653c63a917b55bf0ebdd83c96f041424a92b54b";
    const auto* current = spotify::FindBuildProfile(known);
    assert(current != nullptr);
    assert(current->targets[0].rva == 0xebef18);
    const auto* previous = spotify::FindBuildProfile(
        "65c131dc7f9dcce90eb3874033e6e22ef422a493f0877e3f031805198839b7c4");
    assert(previous != nullptr);
    assert(previous->targets[0].rva == 0xe9d168);
    const auto* v1294 = spotify::FindBuildProfile(
        "0c827576c4394fa2cc7419867d147546a7ea82e52c577ddb9063a95806ea9b11");
    assert(v1294 != nullptr);
    assert(v1294->targets[1].rva == 0xe1b85c);
    const auto* v1292 = spotify::FindBuildProfile(
        "7b44456a90142daeb758e2736d8628ffe211d6ea523db8f1050b3c8b1addb68a");
    assert(v1292 != nullptr);
    assert(v1292->targets[5].rva == 0xdb0fbc);
    assert(spotify::FindBuildProfile(
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") == nullptr);

    spotify::BuildProfile fixture{};
    std::memcpy(fixture.sha256.data(), known, 65);
    for (std::size_t i = 0; i < fixture.targets.size(); ++i) {
        fixture.targets[i].rva = static_cast<std::uint32_t>(0x240 + i * 0x20);
        fixture.targets[i].signature_size = 4;
        fixture.targets[i].signature = {0x48, 0x89, static_cast<std::uint8_t>(i), 0x24};
    }
    auto image = MappedImage(fixture);
    assert(spotify::ValidateMappedProfile(image.data(), image.size(), fixture));
    image[fixture.targets[3].rva] ^= 0xff;
    assert(!spotify::ValidateMappedProfile(image.data(), image.size(), fixture));
    image = MappedImage(fixture);
    Put<std::uint32_t>(image, 0x98 + 240 + 36, 0x40000040);
    assert(!spotify::ValidateMappedProfile(image.data(), image.size(), fixture));
    assert(!spotify::ValidateMappedProfile(image.data(), 512, fixture));
}
