#pragma once

#include <array>
#include <cstddef>

namespace cef_menu {

constexpr std::size_t kBaseBytes = 40;
constexpr std::size_t kKnownDelegateBytes = 96;
constexpr std::size_t kHighestRequiredMenuSlot = 40;
constexpr std::size_t kRequiredMenuBytes =
    kBaseBytes + (kHighestRequiredMenuSlot + 1) * sizeof(void*);
constexpr std::array<std::size_t, 13> kRequiredMenuSlots = {
    0, 1, 2, 3, 4, 5, 7, 15, 20, 23, 28, 36, 40,
};

constexpr bool SupportsDelegate(std::size_t bytes) noexcept {
    return bytes >= kKnownDelegateBytes;
}

inline bool SupportsMenu(std::size_t bytes, void* const* methods,
                         std::size_t method_count) noexcept {
    if (bytes < kRequiredMenuBytes || !methods ||
        method_count <= kHighestRequiredMenuSlot)
        return false;
    for (const auto slot : kRequiredMenuSlots)
        if (!methods[slot]) return false;
    return true;
}

inline bool LooksLikeMainMenu(bool is_submenu, const int* item_types,
                              std::size_t count) noexcept {
    if (is_submenu || !item_types || count < 3 || count > 30) return false;
    std::size_t submenus = 0;
    for (std::size_t index = 0; index < count; ++index)
        if (item_types[index] == 5) ++submenus;
    return submenus >= 3;
}

} // namespace cef_menu
