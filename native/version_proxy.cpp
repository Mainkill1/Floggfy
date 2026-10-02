#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netlistmgr.h>
#include <cstdio>
#include <cstring>
#include "audio_history.h"
#include "hook_init_state.h"
#include "history_settings.h"
#include "metadata_bridge.h"
#include "module_pending.h"
#include "pe_imports.h"
#include "to_disk_menu.h"
#include "startup_ready.h"

// Spotify-local proxy. All version APIs go to the original System32 DLL.
// The connectivity repair and optional native history stay inside Spotify.
static SRWLOCK hook_lock = SRWLOCK_INIT;
static INIT_ONCE version_once = INIT_ONCE_STATIC_INIT;
static HMODULE real_version;
static bool spotify_main;
static HMODULE proxy_module;
static volatile LONG logged_results;
static hooks::PendingModule pending_spotify;
static hooks::InitController connectivity_init;

static void Log(const char* message) {
    history::HistoryLog(message);
}

static BOOL CALLBACK InitVersion(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 20) return FALSE;
    wcscat_s(path, L"\\version.dll");
    real_version = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return real_version != nullptr;
}

extern "C" FARPROC ResolveVersionExport(unsigned index) {
    static const char* names[] = {
        "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
        "GetFileVersionInfoExW", "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",
        "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW", "GetFileVersionInfoW",
        "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
        "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"
    };
    if (index >= sizeof(names) / sizeof(names[0]) ||
        !InitOnceExecuteOnce(&version_once, InitVersion, nullptr, nullptr))
        ExitProcess(ERROR_MOD_NOT_FOUND);
    FARPROC result = GetProcAddress(real_version, names[index]);
    if (!result) ExitProcess(ERROR_PROC_NOT_FOUND);
    return result;
}

extern "C" __declspec(dllexport) const char* SpotifyConnectivityFixVersion() {
    return "SpotifyConnectivityFix/1.0";
}

// Fixed COM ABI slots, not offsets in Spotify's binary. Tables are modified
// only in this process. COM identity, object lifetime, and reference counting
// remain with the original implementation.
enum class Kind { Manager, Enumerator, Connection };
struct TableHook { void** table; Kind kind; void* original[14]; };
static TableHook tables[128];
static unsigned table_count;

template<typename T> static T Original(void* object, unsigned slot) {
    void** table = *reinterpret_cast<void***>(object);
    T method = nullptr;
    AcquireSRWLockShared(&hook_lock);
    for (unsigned i = 0; i < table_count; i++) {
        if (tables[i].table == table) {
            method = reinterpret_cast<T>(tables[i].original[slot]); break;
        }
    }
    ReleaseSRWLockShared(&hook_lock);
    return method;
}

static HRESULT STDMETHODCALLTYPE ManagerConnections(INetworkListManager*, IEnumNetworkConnections**);
static HRESULT STDMETHODCALLTYPE ManagerConnection(INetworkListManager*, GUID, INetworkConnection**);
static HRESULT STDMETHODCALLTYPE EnumNext(IEnumNetworkConnections*, ULONG, INetworkConnection**, ULONG*);
static HRESULT STDMETHODCALLTYPE EnumClone(IEnumNetworkConnections*, IEnumNetworkConnections**);
static HRESULT STDMETHODCALLTYPE ConnectionConnectivity(INetworkConnection*, NLM_CONNECTIVITY*);

static bool HookTable(void* object, Kind kind) {
    if (!object) return false;
    void** table = *reinterpret_cast<void***>(object);
    AcquireSRWLockExclusive(&hook_lock);
    for (unsigned i = 0; i < table_count; i++) {
        if (tables[i].table == table) {
            bool same = tables[i].kind == kind;
            ReleaseSRWLockExclusive(&hook_lock); return same;
        }
    }
    if (table_count == sizeof(tables) / sizeof(tables[0])) {
        ReleaseSRWLockExclusive(&hook_lock); return false;
    }
    unsigned slots = kind == Kind::Enumerator ? 12 : 14;
    DWORD protection;
    if (!VirtualProtect(table, slots * sizeof(void*), PAGE_READWRITE, &protection)) {
        ReleaseSRWLockExclusive(&hook_lock); return false;
    }
    TableHook& record = tables[table_count++];
    record.table = table; record.kind = kind;
    memcpy(record.original, table, slots * sizeof(void*));
    auto put = [table](unsigned slot, void* method) {
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&table[slot]), method);
    };
    switch (kind) {
        case Kind::Manager:
            put(9, reinterpret_cast<void*>(ManagerConnections));
            put(10, reinterpret_cast<void*>(ManagerConnection));
            break;
        case Kind::Enumerator:
            put(8, reinterpret_cast<void*>(EnumNext));
            put(11, reinterpret_cast<void*>(EnumClone));
            break;
        case Kind::Connection:
            put(10, reinterpret_cast<void*>(ConnectionConnectivity));
            break;
    }
    DWORD ignored; VirtualProtect(table, slots * sizeof(void*), protection, &ignored);
    ReleaseSRWLockExclusive(&hook_lock);
    return true;
}

static HRESULT STDMETHODCALLTYPE ManagerConnections(INetworkListManager* self, IEnumNetworkConnections** out) {
    using Method = HRESULT (STDMETHODCALLTYPE*)(INetworkListManager*, IEnumNetworkConnections**);
    Method original = Original<Method>(self, 9);
    if (!original) return E_UNEXPECTED;
    HRESULT result = original(self, out);
    if (SUCCEEDED(result) && out && *out) HookTable(*out, Kind::Enumerator);
    return result;
}
static HRESULT STDMETHODCALLTYPE ManagerConnection(INetworkListManager* self, GUID id, INetworkConnection** out) {
    using Method = HRESULT (STDMETHODCALLTYPE*)(INetworkListManager*, GUID, INetworkConnection**);
    Method original = Original<Method>(self, 10);
    if (!original) return E_UNEXPECTED;
    HRESULT result = original(self, id, out);
    if (SUCCEEDED(result) && out && *out) HookTable(*out, Kind::Connection);
    return result;
}
static HRESULT STDMETHODCALLTYPE EnumNext(IEnumNetworkConnections* self, ULONG count, INetworkConnection** out, ULONG* fetched) {
    using Method = HRESULT (STDMETHODCALLTYPE*)(IEnumNetworkConnections*, ULONG, INetworkConnection**, ULONG*);
    Method original = Original<Method>(self, 8);
    if (!original) return E_UNEXPECTED;
    ULONG local_count = 0;
    HRESULT result = original(self, count, out, &local_count);
    if (fetched) *fetched = local_count;
    if (SUCCEEDED(result) && out) {
        for (ULONG i = 0; i < local_count; i++) if (out[i]) HookTable(out[i], Kind::Connection);
    }
    return result;
}
static HRESULT STDMETHODCALLTYPE EnumClone(IEnumNetworkConnections* self, IEnumNetworkConnections** out) {
    using Method = HRESULT (STDMETHODCALLTYPE*)(IEnumNetworkConnections*, IEnumNetworkConnections**);
    Method original = Original<Method>(self, 11);
    if (!original) return E_UNEXPECTED;
    HRESULT result = original(self, out);
    if (SUCCEEDED(result) && out && *out) HookTable(*out, Kind::Enumerator);
    return result;
}

// This compatibility override intentionally treats a usable adapter address as
// Internet connectivity. It repairs Spotify's broken NLM interpretation; it is
// not an independent Internet reachability test.
static DWORD SyntheticAdapterConnectivity(const GUID& id) {
    ULONG size = 16384;
    auto memory = static_cast<IP_ADAPTER_ADDRESSES*>(HeapAlloc(GetProcessHeap(), 0, size));
    if (!memory) return 0;
    ULONG options = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, options, nullptr, memory, &size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        HeapFree(GetProcessHeap(), 0, memory);
        memory = static_cast<IP_ADAPTER_ADDRESSES*>(HeapAlloc(GetProcessHeap(), 0, size));
        if (!memory) return 0;
        result = GetAdaptersAddresses(AF_UNSPEC, options, nullptr, memory, &size);
    }
    DWORD flags = 0;
    if (result == NO_ERROR) {
        for (auto a = memory; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->AdapterName) continue;
            wchar_t name[80]; GUID adapter;
            if (!MultiByteToWideChar(CP_ACP, 0, a->AdapterName, -1, name, 80) ||
                FAILED(CLSIDFromString(name, &adapter)) || !IsEqualGUID(adapter, id)) continue;
            for (auto u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr) continue;
                if (u->Address.lpSockaddr->sa_family == AF_INET) {
                    auto ip = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                    if (ip->sin_addr.s_addr != INADDR_ANY) flags |= NLM_CONNECTIVITY_IPV4_INTERNET;
                } else if (u->Address.lpSockaddr->sa_family == AF_INET6) {
                    auto ip = reinterpret_cast<sockaddr_in6*>(u->Address.lpSockaddr);
                    if (!IN6_IS_ADDR_UNSPECIFIED(&ip->sin6_addr) &&
                        !IN6_IS_ADDR_LINKLOCAL(&ip->sin6_addr) &&
                        !IN6_IS_ADDR_LOOPBACK(&ip->sin6_addr)) flags |= NLM_CONNECTIVITY_IPV6_INTERNET;
                }
            }
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, memory);
    return flags;
}

static HRESULT STDMETHODCALLTYPE ConnectionConnectivity(INetworkConnection* self, NLM_CONNECTIVITY* out) {
    using Method = HRESULT (STDMETHODCALLTYPE*)(INetworkConnection*, NLM_CONNECTIVITY*);
    Method original = Original<Method>(self, 10);
    if (!original) return E_UNEXPECTED;
    HRESULT result = original(self, out);
    if (SUCCEEDED(result) && out && !(*out & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET))) {
        GUID adapter;
        if (SUCCEEDED(self->GetAdapterId(&adapter))) {
            DWORD flags = SyntheticAdapterConnectivity(adapter);
            if (flags) {
                DWORD previous = static_cast<DWORD>(*out);
                *out = static_cast<NLM_CONNECTIVITY>(previous | flags);
                if (InterlockedIncrement(&logged_results) <= 16) {
                    char message[160];
                    snprintf(message, sizeof(message), "NLM compatibility override original=0x%lX synthetic=0x%lX; adapter has a usable address",
                        static_cast<unsigned long>(previous), static_cast<unsigned long>(*out));
                    Log(message);
                }
            }
        }
    }
    return result;
}

static HRESULT WINAPI RepairCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* out) {
    HRESULT result = CoCreateInstance(clsid, outer, context, iid, out);
    if (SUCCEEDED(result) && out && *out && IsEqualCLSID(clsid, CLSID_NetworkListManager) &&
        IsEqualIID(iid, IID_INetworkListManager)) {
        Log(HookTable(*out, Kind::Manager) ? "NetworkListManager connectivity hook installed" : "NetworkListManager hook failed");
    }
    return result;
}

enum class ImportHookResult { Active, Unsupported, RetryableFailure };

static DWORD MappedImageSize(HMODULE module) {
    if (!module) return 0;
    auto base = reinterpret_cast<BYTE*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

static bool WriteImportSlot(void** slot, void* value, void** previous) {
    if (!slot) return true;
    if (*slot == value) {
        if (previous) *previous = value;
        return true;
    }
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    void* old = InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), value);
    DWORD ignored = 0;
    const bool restored = VirtualProtect(slot, sizeof(void*), protection, &ignored) != FALSE;
    if (!restored) {
        DWORD current = 0;
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &current)) {
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), old);
            VirtualProtect(slot, sizeof(void*), protection, &ignored);
        }
        return false;
    }
    if (previous) *previous = old;
    return true;
}

static ImportHookResult HookSpotifyImports(HMODULE module) {
    const DWORD image_size = MappedImageSize(module);
    if (!image_size) return ImportHookResult::Unsupported;
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    // Search by the API name across every import provider. Windows may expose
    // COM through ole32, combase, or an API-set DLL in different client builds.
    const auto imports = hooks::FindImportSlots(base, image_size, nullptr, "CoCreateInstance");
    void** slots[] = {imports.normal, imports.delay};
    void* previous[2] = {};
    bool changed[2] = {};
    void* replacement = reinterpret_cast<void*>(RepairCoCreateInstance);
    unsigned found = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (!slots[i]) continue;
        ++found;
        const bool was_replacement = *slots[i] == replacement;
        if (!WriteImportSlot(slots[i], replacement, &previous[i])) {
            for (unsigned rollback = 0; rollback < i; ++rollback) {
                if (changed[rollback]) WriteImportSlot(slots[rollback], previous[rollback], nullptr);
            }
            return ImportHookResult::RetryableFailure;
        }
        changed[i] = !was_replacement;
    }
    if (!found) return ImportHookResult::Unsupported;
    if (changed[0] || changed[1]) {
        char message[192];
        snprintf(message, sizeof(message),
                 "connectivity import hook active: normal-IAT=%s delay-IAT=%s%s",
                 imports.normal ? "patched" : "absent", imports.delay ? "patched" : "absent",
                 imports.malformed ? " malformed-table-observed" : "");
        Log(message);
    }
    return ImportHookResult::Active;
}

struct NotificationString { USHORT length, maximum; PWSTR buffer; };
struct NotificationData {
    ULONG flags;
    const NotificationString* full_name;
    const NotificationString* base_name;
    PVOID base;
    ULONG size;
};
static VOID CALLBACK DllNotification(ULONG reason, const NotificationData* data, PVOID) {
    if (reason != 1 || !data || !data->base_name || !data->base_name->buffer) return;
    const auto& name = *data->base_name;
    if (name.length == 11 * sizeof(wchar_t) && hooks::IsSpotifyModuleName(name.buffer, 11))
        pending_spotify.Publish(reinterpret_cast<std::uintptr_t>(data->base));
}

static void StartConnectivityHook(HMODULE module) {
    // Delay-import resolution can replace an IAT slot after our first patch.
    // Keep verifying the live slots from this normal worker context; an
    // already-patched slot is a no-op and does not emit another log record.
    if (connectivity_init.State() == hooks::InitState::Active) {
        HookSpotifyImports(module);
        return;
    }
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    if (!connectivity_init.TryBegin(now)) return;
    switch (HookSpotifyImports(module)) {
        case ImportHookResult::Active:
            connectivity_init.Activate();
            break;
        case ImportHookResult::Unsupported:
            Log("connectivity import hook unavailable: CoCreateInstance import not found or PE unsupported");
            connectivity_init.MarkUnsupported();
            break;
        case ImportHookResult::RetryableFailure:
            Log("connectivity import hook failed temporarily; retry scheduled");
            connectivity_init.Retry(now);
            break;
    }
}

static DWORD WINAPI StartupMonitor(LPVOID) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = length && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
    spotify_main = name && !_wcsicmp(name + 1, L"Spotify.exe") && !wcsstr(GetCommandLineW(), L"--type=");
    if (!spotify_main) return 0;

    using Register = LONG (NTAPI*)(ULONG, decltype(&DllNotification), PVOID, PVOID*);
    FARPROC address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification");
    Register register_notification = nullptr;
    static_assert(sizeof(register_notification) == sizeof(address), "Function pointer sizes must match");
    memcpy(&register_notification, &address, sizeof(address));
    static PVOID notification_cookie = nullptr;
    if (register_notification) register_notification(0, DllNotification, nullptr, &notification_cookie);

    history::InitSettings(proxy_module);
    Log("version.dll proxy loaded; no Windows settings or signed files changed");
    // Polling also covers clients that loaded Spotify.dll before notification
    // registration. Stay alive at low frequency so late loads and temporary
    // allocation or hook failures remain recoverable.
    for (unsigned i = 0;; ++i) {
        const bool spotify_notification = pending_spotify.Consume() != 0;
        (void)spotify_notification;
        HMODULE cef;
        if(GetModuleHandleExW(0,L"libcef.dll",&cef)) { history::StartMetadataCollector(cef); StartToDiskMenu(cef); FreeLibrary(cef); }
        HMODULE module;
        if (GetModuleHandleExW(0, L"Spotify.dll", &module)) {
            StartConnectivityHook(module);
            if (i >= 80) StartAudioHistory(module, proxy_module);
            FreeLibrary(module);
        }
        if(i==0)startup::SignalReady();
        Sleep(i < 600 ? 25 : 1000);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        proxy_module = instance;
        HANDLE thread = CreateThread(nullptr, 0, StartupMonitor, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
