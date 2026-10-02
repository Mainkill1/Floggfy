#include "../native/cef_menu_capability.h"

#include <array>
#include <cassert>
#include <cstddef>

int main() {
    assert(cef_menu::SupportsDelegate(cef_menu::kKnownDelegateBytes));
    assert(cef_menu::SupportsDelegate(cef_menu::kKnownDelegateBytes + 64));
    assert(!cef_menu::SupportsDelegate(cef_menu::kKnownDelegateBytes - 1));

    std::array<void*, 56> methods{};
    for (const auto slot : cef_menu::kRequiredMenuSlots)
        methods[slot] = reinterpret_cast<void*>(std::size_t{0x1000} + slot);
    assert(cef_menu::SupportsMenu(cef_menu::kRequiredMenuBytes,
                                  methods.data(), methods.size()));
    assert(cef_menu::SupportsMenu(cef_menu::kRequiredMenuBytes + 128,
                                  methods.data(), methods.size()));
    assert(!cef_menu::SupportsMenu(cef_menu::kRequiredMenuBytes - 1,
                                   methods.data(), methods.size()));
    methods[cef_menu::kRequiredMenuSlots[3]] = nullptr;
    assert(!cef_menu::SupportsMenu(cef_menu::kRequiredMenuBytes,
                                   methods.data(), methods.size()));

    constexpr int command = 1;
    constexpr int submenu = 5;
    const int localized_top_level[] = {submenu, submenu, submenu, command};
    assert(cef_menu::LooksLikeMainMenu(false, localized_top_level, 4));
    assert(!cef_menu::LooksLikeMainMenu(true, localized_top_level, 4));
    const int context_menu[] = {command, submenu, command, submenu};
    assert(!cef_menu::LooksLikeMainMenu(false, context_menu, 4));
    assert(!cef_menu::LooksLikeMainMenu(false, localized_top_level, 31));
}
