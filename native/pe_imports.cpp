#include "pe_imports.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace hooks {
namespace {

template<class T>
bool Read(const std::uint8_t* image, std::size_t size, std::size_t offset, T& value) noexcept {
    if (offset > size || sizeof(T) > size - offset) return false;
    std::memcpy(&value, image + offset, sizeof(T));
    return true;
}

bool Range(std::size_t size, std::uint32_t rva, std::size_t bytes) noexcept {
    return rva <= size && bytes <= size - rva;
}

bool AddRva(std::uint32_t base, std::uint64_t delta, std::uint32_t& result) noexcept {
    if (delta > std::numeric_limits<std::uint32_t>::max() - base) return false;
    result = base + static_cast<std::uint32_t>(delta);
    return true;
}

char Lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool StringEquals(const std::uint8_t* image, std::size_t size, std::uint32_t rva,
                  const char* expected, bool insensitive) noexcept {
    if (!expected || rva >= size) return false;
    for (std::size_t i = 0; i < size - rva; ++i) {
        const char actual = static_cast<char>(image[rva + i]);
        const char wanted = expected[i];
        if ((insensitive ? Lower(actual) : actual) !=
            (insensitive ? Lower(wanted) : wanted)) return false;
        if (actual == '\0') return true;
        if (wanted == '\0') return false;
    }
    return false;
}

bool NamedThunk(const std::uint8_t* image, std::size_t size, std::uint64_t thunk,
                const char* function_name) noexcept {
    constexpr std::uint64_t ordinal = std::uint64_t{1} << 63;
    if (!thunk || (thunk & ordinal) || thunk > std::numeric_limits<std::uint32_t>::max())
        return false;
    const auto rva = static_cast<std::uint32_t>(thunk);
    return Range(size, rva, 3) && StringEquals(image, size, rva + 2, function_name, false);
}

void ScanNormal(std::uint8_t* image, std::size_t size, std::uint32_t directory_rva,
                std::uint32_t directory_size, const char* dll_name,
                const char* function_name, ImportSlots& result) noexcept {
    constexpr std::size_t descriptor_size = 20;
    if (!directory_rva && !directory_size) return;
    if (!Range(size, directory_rva, directory_size) || directory_size < descriptor_size) {
        result.malformed = true;
        return;
    }
    for (std::size_t offset = 0; offset + descriptor_size <= directory_size;
         offset += descriptor_size) {
        const std::size_t descriptor = directory_rva + offset;
        std::uint32_t names = 0, name = 0, iat = 0;
        if (!Read(image, size, descriptor, names) ||
            !Read(image, size, descriptor + 12, name) ||
            !Read(image, size, descriptor + 16, iat)) {
            result.malformed = true;
            return;
        }
        if (!names && !name && !iat) return;
        if (!StringEquals(image, size, name, dll_name, true)) continue;
        if (!iat) { result.malformed = true; return; }
        if (!names) continue; // valid bound import without a name thunk cannot be resolved by name
        for (std::uint32_t index = 0;; ++index) {
            const std::uint64_t delta = std::uint64_t{index} * 8;
            std::uint32_t name_rva = 0, iat_rva = 0;
            if (!AddRva(names, delta, name_rva) || !AddRva(iat, delta, iat_rva) ||
                !Range(size, name_rva, 8) || !Range(size, iat_rva, 8)) {
                result.malformed = true;
                return;
            }
            std::uint64_t thunk = 0;
            Read(image, size, name_rva, thunk);
            if (!thunk) break;
            if (NamedThunk(image, size, thunk, function_name)) {
                result.normal = reinterpret_cast<void**>(image + iat_rva);
                return;
            }
        }
    }
}

void ScanDelay(std::uint8_t* image, std::size_t size, std::uint32_t directory_rva,
               std::uint32_t directory_size, const char* dll_name,
               const char* function_name, ImportSlots& result) noexcept {
    constexpr std::size_t descriptor_size = 32;
    if (!directory_rva && !directory_size) return;
    if (!Range(size, directory_rva, directory_size) || directory_size < descriptor_size) {
        result.malformed = true;
        return;
    }
    for (std::size_t offset = 0; offset + descriptor_size <= directory_size;
         offset += descriptor_size) {
        const std::size_t descriptor = directory_rva + offset;
        std::uint32_t attributes = 0, name = 0, iat = 0, names = 0;
        if (!Read(image, size, descriptor, attributes) ||
            !Read(image, size, descriptor + 4, name) ||
            !Read(image, size, descriptor + 12, iat) ||
            !Read(image, size, descriptor + 16, names)) {
            result.malformed = true;
            return;
        }
        if (!attributes && !name && !iat && !names) return;
        if (!(attributes & 1) || !StringEquals(image, size, name, dll_name, true)) continue;
        if (!iat || !names) { result.malformed = true; return; }
        for (std::uint32_t index = 0;; ++index) {
            const std::uint64_t delta = std::uint64_t{index} * 8;
            std::uint32_t name_rva = 0, iat_rva = 0;
            if (!AddRva(names, delta, name_rva) || !AddRva(iat, delta, iat_rva) ||
                !Range(size, name_rva, 8) || !Range(size, iat_rva, 8)) {
                result.malformed = true;
                return;
            }
            std::uint64_t thunk = 0;
            Read(image, size, name_rva, thunk);
            if (!thunk) break;
            if (NamedThunk(image, size, thunk, function_name)) {
                result.delay = reinterpret_cast<void**>(image + iat_rva);
                return;
            }
        }
    }
}

} // namespace

ImportSlots FindImportSlots(std::uint8_t* image, std::size_t available,
                            const char* dll_name, const char* function_name) noexcept {
    ImportSlots result;
    if (!image || available < 0x40 || !dll_name || !function_name) {
        result.malformed = true;
        return result;
    }
    std::uint16_t dos = 0;
    std::uint32_t nt_rva = 0;
    if (!Read(image, available, 0, dos) || dos != 0x5a4d ||
        !Read(image, available, 0x3c, nt_rva) || nt_rva > 0x100000) {
        result.malformed = true;
        return result;
    }
    std::uint32_t signature = 0;
    std::uint16_t machine = 0, optional_size = 0, magic = 0;
    const std::size_t optional = static_cast<std::size_t>(nt_rva) + 24;
    if (!Read(image, available, nt_rva, signature) || signature != 0x00004550 ||
        !Read(image, available, nt_rva + 4, machine) || machine != 0x8664 ||
        !Read(image, available, nt_rva + 20, optional_size) || optional_size < 240 ||
        !Read(image, available, optional, magic) || magic != 0x20b ||
        optional > available || optional_size > available - optional) {
        result.malformed = true;
        return result;
    }
    std::uint32_t declared_size = 0, directories = 0;
    if (!Read(image, available, optional + 56, declared_size) ||
        !Read(image, available, optional + 108, directories) || !declared_size) {
        result.malformed = true;
        return result;
    }
    const std::size_t image_size = std::min<std::size_t>(available, declared_size);
    if (declared_size > available) result.malformed = true;
    auto directory = [&](std::uint32_t index, std::uint32_t& rva, std::uint32_t& size) {
        if (index >= directories || 112 + (index + 1) * 8 > optional_size) return false;
        return Read(image, image_size, optional + 112 + index * 8, rva) &&
               Read(image, image_size, optional + 116 + index * 8, size);
    };
    std::uint32_t rva = 0, size = 0;
    if (directory(1, rva, size))
        ScanNormal(image, image_size, rva, size, dll_name, function_name, result);
    rva = size = 0;
    if (directory(13, rva, size))
        ScanDelay(image, image_size, rva, size, dll_name, function_name, result);
    return result;
}

} // namespace hooks
