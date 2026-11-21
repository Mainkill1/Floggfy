#pragma once
#include <windows.h>
namespace network {
// Call once during startup. Hidden (-1) succeeds without attaching hooks.
// Helpers read the INI beside proxy and join the controller's shared state.
// This covers Winsock IPv4/IPv6 calls in injected processes, including known
// extension pointers discovered after initialization. It is not OS enforcement:
// already queued I/O, prior cached provider/RIO pointers, native NT socket I/O,
// DNS requests serviced outside this process, and uninjected helpers can bypass.
bool Initialize(HMODULE proxy, bool controlling, int initial_mode);
bool SetEnabled(bool enabled);
bool Enabled();
bool Available();
}
