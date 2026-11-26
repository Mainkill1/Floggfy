#pragma once
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cstring>
#include <string_view>

namespace updates {
inline constexpr char original_url[] = "https://spclient.wg.spotify.com/desktop-update/v2/update";
inline constexpr char loopback_url[] = "https://127.0.0.1/desktop-updates/are/not/desired/thanks";
static_assert(sizeof(original_url) == 57 && sizeof(original_url) == sizeof(loopback_url));

inline const char* RedirectUpdateUrl(HMODULE module) {
    MODULEINFO info{};
    if (!K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return nullptr;
    const auto base = static_cast<char*>(info.lpBaseOfDll);
    const size_t size = info.SizeOfImage;
    if (size < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < LONG(sizeof(*dos)) ||
        size_t(dos->e_lfanew) > size || size - size_t(dos->e_lfanew) < sizeof(IMAGE_NT_HEADERS64)) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)) return nullptr;
    const size_t table = size_t(dos->e_lfanew) + sizeof(*nt);
    if (nt->FileHeader.NumberOfSections > (size - table) / sizeof(IMAGE_SECTION_HEADER)) return nullptr;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + table);

    constexpr std::string_view original(original_url, sizeof(original_url));
    constexpr std::string_view redirected(loopback_url, sizeof(loopback_url));
    char* match = nullptr;
    bool already_redirected = false;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        constexpr DWORD data = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        constexpr DWORD skip = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_DISCARDABLE;
        if ((section.Characteristics & data) != data || (section.Characteristics & skip) ||
            section.VirtualAddress >= size) continue;
        const auto length = std::min<size_t>(section.Misc.VirtualSize, size - section.VirtualAddress);
        auto* start = base + section.VirtualAddress;
        const auto* end = start + length;
        while (start < end) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(start, &region, sizeof(region))) return nullptr;
            const auto* region_base = static_cast<const char*>(region.BaseAddress);
            const auto count = std::min<size_t>(end - start, region.RegionSize - (start - region_base));
            if (region.State == MEM_COMMIT && (region.Protect == PAGE_READONLY ||
                    region.Protect == PAGE_READWRITE || region.Protect == PAGE_WRITECOPY)) {
                const std::string_view bytes(start, count);
                const auto found = bytes.find(original);
                const auto patched = bytes.find(redirected);
                if (found != bytes.npos || patched != bytes.npos) {
                    if (match || (found != bytes.npos && patched != bytes.npos)) return nullptr;
                    already_redirected = patched != bytes.npos;
                    const auto needle = already_redirected ? redirected : original;
                    const auto offset = already_redirected ? patched : found;
                    if (bytes.find(needle, offset + 1) != bytes.npos) return nullptr;
                    match = start + offset;
                }
            }
            start += count;
        }
    }
    if (!match || already_redirected) return match;

    // Change private image data. The signed file on disk remains intact.
    DWORD previous = 0;
    if (!VirtualProtect(match, original.size(), PAGE_WRITECOPY, &previous)) return nullptr;
    std::memcpy(match, redirected.data(), redirected.size() - 1);
    DWORD ignored = 0;
    return VirtualProtect(match, original.size(), previous, &ignored) ? match : nullptr;
}

inline bool UpdateUrlStillRedirected(const char* address) {
    char value[sizeof(loopback_url)]{};
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, value, sizeof(value), &count) &&
           count == sizeof(value) && std::memcmp(value, loopback_url, sizeof(value)) == 0;
}
}
