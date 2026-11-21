#pragma once
#include <cstddef>
#include <cstdint>

namespace history {
inline constexpr std::uint64_t kFnv1aOffset = 14695981039346656037ULL;

inline constexpr std::uint64_t Fnv1aByte(std::uint64_t hash, std::uint8_t byte) noexcept {
    return (hash ^ byte) * 1099511628211ULL;
}
inline std::uint64_t Fnv1a64(const void* bytes, std::size_t size,
                             std::uint64_t hash = kFnv1aOffset) noexcept {
    const auto* data = static_cast<const unsigned char*>(bytes);
    for (std::size_t i = 0; i < size; ++i) hash = Fnv1aByte(hash, data[i]);
    return hash;
}
}
