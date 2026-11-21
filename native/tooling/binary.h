#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace binary {
// Callers validate the available bytes before reading.
inline std::uint16_t ReadBig16(const std::uint8_t* bytes) noexcept {
    return std::uint16_t((std::uint16_t(bytes[0]) << 8) | bytes[1]);
}
inline std::uint16_t ReadLittle16(const std::uint8_t* bytes) noexcept {
    return std::uint16_t(bytes[0] | (std::uint16_t(bytes[1]) << 8));
}
inline std::uint32_t ReadBig32(const std::uint8_t* bytes) noexcept {
    return (std::uint32_t(ReadBig16(bytes)) << 16) | ReadBig16(bytes + 2);
}
inline std::uint32_t ReadLittle32(const std::uint8_t* bytes) noexcept {
    return ReadLittle16(bytes) | (std::uint32_t(ReadLittle16(bytes + 2)) << 16);
}
inline std::uint64_t ReadBig64(const std::uint8_t* bytes) noexcept {
    return (std::uint64_t(ReadBig32(bytes)) << 32) | ReadBig32(bytes + 4);
}

namespace detail {
constexpr auto Crc32Table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto crc = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
        table[i] = crc;
    }
    return table;
}
inline constexpr auto crc32_table = Crc32Table();
}

inline std::uint32_t Crc32(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i)
        crc = (crc >> 8) ^ detail::crc32_table[(crc ^ bytes[i]) & 255];
    return ~crc;
}
}
