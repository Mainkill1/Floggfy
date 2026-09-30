#include "spotify_hook_discovery.h"

#include <algorithm>
#include <cstring>

namespace spotify {
namespace {

constexpr std::uint32_t kExecutable = 0x20000000;
constexpr std::size_t kSectionSize = 40;
constexpr std::size_t kRuntimeFunctionSize = 12;

constexpr std::uint8_t kOggPageSeek[] = {
    0x0f,0xb6,0x47,0x1a,0x8d,0x48,0x1b,0x3b,0xe9,0x0f,0x8c,0x1e,0x01,
    0x00,0x00,0x33,0xd2,0x85,0xc0,0x74,0x20,0x44,0x8b,0x43,0x1c,
};
constexpr std::uint8_t kFormatSniff[] = {
    0x48,0x89,0x55,0x88,0x4c,0x8b,0xf9,0xc7,0x44,0x24,0x3c,0x4f,0x67,
    0x67,0x53,0xc7,0x44,0x24,0x40,0x53,0x70,0x41,0x43,0x66,0xc7,0x44,
    0x24,0x38,0x49,0x44,0xc6,0x44,0x24,0x3a,0x33,0xc7,0x44,0x24,0x44,
    0x66,0x74,0x79,0x70,0xc7,0x44,0x24,0x48,0x66,0x4c,0x61,0x43,0xc7,
    0x44,0x24,0x4c,0x52,0x49,0x46,0x46,0xc7,0x44,0x24,0x50,0x57,0x41,
    0x56,0x45,
};
constexpr std::uint8_t kFlacInit[] = {
    0x48,0x8b,0x57,0x20,0x48,0x89,0x47,0x20,0x48,0x85,0xd2,0x74,0x0a,
    0x48,0x8b,0x47,0x18,0x48,0x8b,0xcf,0xff,
};
constexpr std::uint8_t kFlacRead[] = {
    0x4c,0x8d,0x41,0x10,0x40,0x38,0x79,0x38,0x74,0x09,0x49,0x8b,0x48,
    0x08,0x49,0x2b,0x08,0xeb,0x1e,0x49,0x8b,0x48,0x20,0x49,0x3b,0x48,
    0x18,0x72,0x06,0x49,0x2b,0x48,0x18,
};
constexpr std::uint8_t kFlacFrame[] = {
    0x83,0x7a,0x08,0x02,0x75,0x64,0x48,0x39,0x41,0x08,0x74,0x5e,0x49,
    0x39,0x00,0x74,0x59,0x49,0x39,0x40,0x08,0x74,0x53,0x83,0x7a,0x10,
    0x10,0x74,0x06,0x83,0x7a,0x10,0x18,0x75,0x47,
};
constexpr std::uint8_t kFlacError[] = {
    0x48,0x8b,0xd9,0x48,0x63,0xfa,0x75,0x2b,0x85,0xd2,0x75,0x27,
};

struct Pattern {
    TargetKind kind;
    const std::uint8_t* bytes;
    std::size_t size;
    std::uint32_t minimum_function_size;
    std::uint32_t maximum_function_size;
};

constexpr Pattern kPatterns[] = {
    {TargetKind::OggPageSeek, kOggPageSeek, sizeof(kOggPageSeek), 0x140, 0x300},
    {TargetKind::FormatSniff, kFormatSniff, sizeof(kFormatSniff), 0x800, 0x1000},
    {TargetKind::FlacInit, kFlacInit, sizeof(kFlacInit), 0x180, 0x400},
    {TargetKind::FlacRead, kFlacRead, sizeof(kFlacRead), 0xa0, 0x200},
    {TargetKind::FlacFrame, kFlacFrame, sizeof(kFlacFrame), 0x80, 0x200},
    {TargetKind::FlacError, kFlacError, sizeof(kFlacError), 0x60, 0x200},
};

template<class T>
bool Read(const std::uint8_t* image, std::size_t size, std::size_t offset,
          T& value) noexcept {
    if (offset > size || sizeof(T) > size - offset) return false;
    std::memcpy(&value, image + offset, sizeof(T));
    return true;
}

bool Range(std::size_t size, std::size_t offset, std::size_t length) noexcept {
    return offset <= size && length <= size - offset;
}

struct Image {
    const std::uint8_t* base = nullptr;
    std::size_t available = 0;
    std::uint32_t image_size = 0;
    std::size_t sections = 0;
    std::uint16_t section_count = 0;
    std::uint32_t exceptions = 0;
    std::uint32_t exception_size = 0;
};

bool ParseImage(const std::uint8_t* base, std::size_t available,
                Image& image) noexcept {
    if (!base || available < 0x40) return false;
    std::uint16_t dos = 0, machine = 0, optional_size = 0, magic = 0;
    std::uint32_t nt = 0, signature = 0, directory_count = 0;
    if (!Read(base, available, 0, dos) || dos != 0x5a4d ||
        !Read(base, available, 0x3c, nt) || nt > 0x100000 ||
        !Read(base, available, nt, signature) || signature != 0x00004550 ||
        !Read(base, available, nt + 4, machine) || machine != 0x8664 ||
        !Read(base, available, nt + 6, image.section_count) || !image.section_count ||
        !Read(base, available, nt + 20, optional_size) || optional_size < 240 ||
        !Read(base, available, nt + 24, magic) || magic != 0x20b ||
        !Read(base, available, nt + 24 + 56, image.image_size) ||
        !image.image_size || image.image_size > available ||
        !Read(base, available, nt + 24 + 108, directory_count) ||
        directory_count <= 3 ||
        !Read(base, available, nt + 24 + 112 + 3 * 8, image.exceptions) ||
        !Read(base, available, nt + 24 + 112 + 3 * 8 + 4, image.exception_size))
        return false;
    image.sections = std::size_t{nt} + 24 + optional_size;
    image.base = base;
    image.available = available;
    return Range(available, image.sections,
                 std::size_t{image.section_count} * kSectionSize) &&
           image.exception_size >= kRuntimeFunctionSize &&
           image.exception_size % kRuntimeFunctionSize == 0 &&
           Range(image.image_size, image.exceptions, image.exception_size);
}

bool ExecutableRange(const Image& image, std::uint32_t rva,
                     std::size_t length) noexcept {
    const std::uint64_t finish = std::uint64_t{rva} + length;
    for (std::uint16_t index = 0; index < image.section_count; ++index) {
        const std::size_t section = image.sections + std::size_t{index} * kSectionSize;
        std::uint32_t virtual_size = 0, virtual_address = 0, raw_size = 0, flags = 0;
        if (!Read(image.base, image.available, section + 8, virtual_size) ||
            !Read(image.base, image.available, section + 12, virtual_address) ||
            !Read(image.base, image.available, section + 16, raw_size) ||
            !Read(image.base, image.available, section + 36, flags)) return false;
        const std::uint64_t end = std::uint64_t{virtual_address} +
                                  std::max(virtual_size, raw_size);
        if ((flags & kExecutable) && rva >= virtual_address && finish <= end &&
            finish <= image.image_size) return true;
    }
    return false;
}

bool RuntimeFunction(const Image& image, std::uint32_t instruction,
                     std::size_t length, std::uint32_t& begin,
                     std::uint32_t& end) noexcept {
    const std::size_t count = image.exception_size / kRuntimeFunctionSize;
    std::size_t low = 0, high = count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        std::uint32_t candidate = 0;
        if (!Read(image.base, image.available,
                  image.exceptions + middle * kRuntimeFunctionSize, candidate)) return false;
        if (candidate <= instruction) low = middle + 1; else high = middle;
    }
    if (!low) return false;
    const std::size_t entry = image.exceptions + (low - 1) * kRuntimeFunctionSize;
    if (!Read(image.base, image.available, entry, begin) ||
        !Read(image.base, image.available, entry + 4, end) ||
        begin >= end || instruction < begin ||
        std::uint64_t{instruction} + length > end ||
        !ExecutableRange(image, begin, end - begin)) return false;
    return true;
}

DiscoveryResult FindTarget(const Image& image, const Pattern& pattern,
                           HookTarget& target) noexcept {
    std::uint32_t function = 0;
    bool found = false;
    for (std::uint16_t index = 0; index < image.section_count; ++index) {
        const std::size_t section = image.sections + std::size_t{index} * kSectionSize;
        std::uint32_t virtual_size = 0, virtual_address = 0, raw_size = 0, flags = 0;
        if (!Read(image.base, image.available, section + 8, virtual_size) ||
            !Read(image.base, image.available, section + 12, virtual_address) ||
            !Read(image.base, image.available, section + 16, raw_size) ||
            !Read(image.base, image.available, section + 36, flags))
            return DiscoveryResult::InvalidImage;
        if (!(flags & kExecutable) || virtual_address >= image.image_size) continue;
        const std::size_t extent = std::min<std::size_t>(
            std::max(virtual_size, raw_size), image.image_size - virtual_address);
        if (extent < pattern.size) continue;
        const std::uint8_t* const section_start = image.base + virtual_address;
        const std::uint8_t* cursor = section_start;
        const std::uint8_t* const finish = cursor + extent - pattern.size + 1;
        while (cursor < finish) {
            const void* next = std::memchr(cursor, pattern.bytes[0],
                                           static_cast<std::size_t>(finish - cursor));
            if (!next) break;
            cursor = static_cast<const std::uint8_t*>(next);
            if (std::memcmp(cursor, pattern.bytes, pattern.size) == 0) {
                const std::uint32_t hit = virtual_address +
                    static_cast<std::uint32_t>(cursor - section_start);
                std::uint32_t begin = 0, end = 0;
                if (RuntimeFunction(image, hit, pattern.size, begin, end) &&
                    end - begin >= pattern.minimum_function_size &&
                    end - begin <= pattern.maximum_function_size) {
                    if (found && function != begin)
                        return DiscoveryResult::AmbiguousTarget;
                    found = true;
                    function = begin;
                }
            }
            ++cursor;
        }
    }
    if (!found) return DiscoveryResult::MissingTarget;
    target = HookTarget{pattern.kind, function};
    return DiscoveryResult::Found;
}

bool Gap(std::uint32_t lower, std::uint32_t upper,
         std::uint32_t minimum, std::uint32_t maximum) noexcept {
    return upper > lower && upper - lower >= minimum && upper - lower <= maximum;
}

bool Consistent(const HookTargets& targets) noexcept {
    const auto error = TargetRva(targets, TargetKind::FlacError);
    const auto init = TargetRva(targets, TargetKind::FlacInit);
    const auto read = TargetRva(targets, TargetKind::FlacRead);
    const auto frame = TargetRva(targets, TargetKind::FlacFrame);
    const auto sniff = TargetRva(targets, TargetKind::FormatSniff);
    const auto ogg = TargetRva(targets, TargetKind::OggPageSeek);
    return Gap(error, init, 0x400, 0x800) &&
           Gap(init, read, 0x500, 0xa00) &&
           Gap(read, frame, 0x200, 0x600) &&
           Gap(frame, sniff, 0x3000, 0x10000) &&
           Gap(sniff, ogg, 0x10000, 0x40000);
}

} // namespace

DiscoveryResult DiscoverHookTargets(const std::uint8_t* mapped_image,
                                    std::size_t available,
                                    HookTargets& targets) noexcept {
    targets = {};
    Image image{};
    if (!ParseImage(mapped_image, available, image))
        return DiscoveryResult::InvalidImage;
    for (std::size_t index = 0; index < std::size(kPatterns); ++index) {
        const auto result = FindTarget(image, kPatterns[index], targets.entries[index]);
        if (result != DiscoveryResult::Found) {
            targets = {};
            return result;
        }
    }
    if (!Consistent(targets)) {
        targets = {};
        return DiscoveryResult::InconsistentLayout;
    }
    return DiscoveryResult::Found;
}

std::uint32_t TargetRva(const HookTargets& targets, TargetKind kind) noexcept {
    for (const auto& target : targets.entries)
        if (target.kind == kind) return target.rva;
    return 0;
}

} // namespace spotify
