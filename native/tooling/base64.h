#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace base64 {
constexpr std::size_t EncodedSize(std::size_t size) noexcept {
    return 4 * (size / 3 + (size % 3 != 0));
}

inline void Append(std::string& out, std::string_view bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const bool second = bytes.size() - i >= 2, third = bytes.size() - i >= 3;
        const unsigned value = (unsigned(static_cast<unsigned char>(bytes[i])) << 16)
            | (second ? unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8 : 0)
            | (third ? static_cast<unsigned char>(bytes[i + 2]) : 0);
        const char chunk[] = {alphabet[value >> 18], alphabet[(value >> 12) & 63],
            second ? alphabet[(value >> 6) & 63] : '=', third ? alphabet[value & 63] : '='};
        out.append(chunk, sizeof(chunk));
    }
}

enum class Alphabet { Standard, Url };

constexpr int Digit(unsigned c, Alphabet alphabet = Alphabet::Standard) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == (alphabet == Alphabet::Url ? '-' : '+')) return 62;
    if (c == (alphabet == Alphabet::Url ? '_' : '/')) return 63;
    return -1;
}

// Standard padded Base64. The whitespace option is for stored Vorbis comments.
template<class Char, class Output>
bool DecodePadded(std::basic_string_view<Char> input, std::size_t limit, Output& out,
                  bool allow_whitespace = false) {
    if (!allow_whitespace && (input.size() % 4 || input.size() / 4 > EncodedSize(limit) / 4))
        return false;
    out.clear();
    out.reserve((std::min)(input.size() / 4 * 3, limit));
    int group[4]{};
    unsigned used = 0;
    bool padded = false;
    for (Char c : input) {
        if (allow_whitespace && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) continue;
        if (padded) return false;
        const int digit = c == '=' ? -2 : Digit(c);
        if (digit == -1) return false;
        group[used++] = digit;
        if (used != 4) continue;
        used = 0;
        const auto& [a, b, c_value, d] = group;
        if (a < 0 || b < 0) return false;
        const unsigned count = c_value < 0 ? 1 : d < 0 ? 2 : 3;
        if ((count == 1 && (d >= 0 || (b & 15))) ||
            (count == 2 && (c_value & 3)) || count > limit - out.size()) return false;
        using Byte = typename Output::value_type;
        out.push_back(static_cast<Byte>((a << 2) | (b >> 4)));
        if (count > 1) out.push_back(static_cast<Byte>((b << 4) | (c_value >> 2)));
        if (count > 2) out.push_back(static_cast<Byte>((c_value << 6) | d));
        padded = count < 3;
    }
    return used == 0;
}
}
