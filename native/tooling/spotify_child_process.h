#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace spotify_child {
using CreateProcess=decltype(&CreateProcessW);
struct Status {
 bool requested=false;
 std::uint64_t loaded=0;
 std::uint64_t failed=0;
 std::uint64_t unsupported_debug=0;
};

// Launcher mode is identified by the exact loaded module name Floggfy.dll.
bool Configure(HMODULE module) noexcept;
bool Enabled() noexcept;
Status GetStatus() noexcept;

BOOL Create(CreateProcess original,LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process) noexcept;
}
