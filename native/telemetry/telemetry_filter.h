#pragma once
#include <windows.h>
#include <string_view>
namespace privacy {
// Prepare only creates/queues the standalone root. The caller applies every
// startup owner together, then reports the shared result to Finalize.
// Restart-only preference; hooks and trampoline state have process lifetime.
bool Prepare(HMODULE cef,bool enabled) noexcept;
void Finalize(bool applied) noexcept;
void ObserveClient(void* client) noexcept;
void DrainStatus() noexcept;
bool Enabled() noexcept;
void ObserveRenderer(std::wstring_view fixed_status) noexcept;
void ObserveRendererCapabilities(std::wstring_view fixed_status) noexcept;
void ObserveRendererCount(std::wstring_view fixed_count) noexcept;
void ObserveRendererDump(std::wstring_view bounded_record) noexcept;
}
