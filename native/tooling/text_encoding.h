#pragma once
#include <cstddef>
#include <string_view>

namespace text_encoding {
enum class Ascii { Any, NoNull, Printable };

inline bool ValidUtf8(std::string_view input, Ascii ascii) noexcept {
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i++]);
        if (first < 128) {
            if (ascii == Ascii::NoNull && first == 0) return false;
            if (ascii == Ascii::Printable &&
                ((first < 32 && first != '\n' && first != '\r' && first != '\t') || first == 127))
                return false;
            continue;
        }
        const unsigned count = first >= 0xc2 && first <= 0xdf ? 1
            : first >= 0xe0 && first <= 0xef ? 2
            : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
        if (!count || count > input.size() - i) return false;
        unsigned value = first & ((1u << (6 - count)) - 1);
        for (unsigned byte = 0; byte < count; ++byte) {
            const auto next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        constexpr unsigned minimum[] = {0, 0x80, 0x800, 0x10000};
        if (value < minimum[count] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

constexpr int HexDigit(unsigned c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// On success, consume two characters after a percent sign.
inline bool ReadPercentByte(std::string_view text, std::size_t& at, char& byte) noexcept {
    if (at >= text.size() || text.size() - at < 3) return false;
    const int high = HexDigit(text[at + 1]), low = HexDigit(text[at + 2]);
    if (high < 0 || low < 0) return false;
    byte = static_cast<char>(high * 16 + low);
    at += 2;
    return true;
}
}
