#pragma once
#include <windows.h>
namespace network {
// Runs cleanup/restart in a separate process so no Spotify queues remain in RAM.
bool RequestReconnect(bool enabled) noexcept;
}
