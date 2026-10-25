#pragma once
#include <windows.h>
namespace history {
// Optional restart-only observer. Unsupported layouts leave capture untouched.
void StartDecodedConnectMessages(HMODULE spotify,bool requested) noexcept;
// Existing startup worker drains bounded copied observations; no extra thread.
void DrainDecodedConnectMessages() noexcept;
}
