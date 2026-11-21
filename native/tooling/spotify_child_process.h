#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace spotify_child {
using CreateProcessFunction=decltype(&CreateProcessW);
struct Status {
 bool enabled=false;
 std::uint64_t loaded=0;
 std::uint64_t failed=0;
 std::uint64_t unsupported_debug=0;
};

// 0 disables recursion, 1 accepts only launcher-loaded Floggfy.dll, and 2
// accepts any module path. Covered children receive that exact module before
// application entry and recursively cover their own descendants.
int ConfiguredMode(HMODULE module) noexcept;
bool Configure(HMODULE module,int mode) noexcept;
bool Enabled() noexcept;
Status GetStatus() noexcept;

BOOL Create(CreateProcessFunction original,LPCWSTR application,LPWSTR command,
 LPSECURITY_ATTRIBUTES process_attributes,LPSECURITY_ATTRIBUTES thread_attributes,
 BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,
 LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process) noexcept;
}
