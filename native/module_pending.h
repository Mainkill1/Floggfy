#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hooks {

constexpr wchar_t AsciiLower(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
}

inline bool IsSpotifyModuleName(const wchar_t* name, std::size_t characters) noexcept {
    constexpr wchar_t expected[] = L"spotify.dll";
    if (!name || characters != 11) return false;
    for (std::size_t i = 0; i < 11; ++i) {
        if (AsciiLower(name[i]) != expected[i]) return false;
    }
    return true;
}

class PendingModule {
public:
    void Publish(std::uintptr_t module) noexcept {
        value_.store(module, std::memory_order_release);
    }

    std::uintptr_t Consume() noexcept {
        return value_.exchange(0, std::memory_order_acq_rel);
    }

private:
    static_assert(std::atomic<std::uintptr_t>::is_always_lock_free,
                  "loader notification publication must be lock-free");
    std::atomic<std::uintptr_t> value_{0};
};

} // namespace hooks
