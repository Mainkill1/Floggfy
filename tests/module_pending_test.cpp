#include "../native/module_pending.h"

#include <cassert>
#include <cstdint>

int main() {
    hooks::PendingModule pending;
    assert(hooks::IsSpotifyModuleName(L"Spotify.dll", 11));
    assert(hooks::IsSpotifyModuleName(L"sPoTiFy.DlL", 11));
    assert(!hooks::IsSpotifyModuleName(L"Spotify.exe", 11));
    assert(!hooks::IsSpotifyModuleName(L"Spotify.dllx", 12));

    assert(pending.Consume() == 0);
    pending.Publish(0x1234);
    assert(pending.Consume() == static_cast<std::uintptr_t>(0x1234));
    assert(pending.Consume() == 0);
}
