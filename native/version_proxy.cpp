#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netlistmgr.h>
#include "tooling/windows_known_folder.h"
#include <cstdio>
#include <cstring>
#include "media/audio_history.h"
#include "tooling/connectivity_diagnostics.h"
#include "media/connect_messages_windows.h"
#include "tooling/hook_init_state.h"
#include "tooling/hook_startup_minhook.h"
#include "tooling/history_settings.h"
#include "ui/marked_catalog.h"
#include "tooling/async_log.h"
#include "tooling/hook_diagnostics.h"
#include "media/metadata_bridge.h"
#include "telemetry/telemetry_filter.h"
#include "telemetry/telemetry_options.h"
#include "telemetry/telemetry_crash.h"
#include "telemetry/telemetry_queue.h"
#include "telemetry/telemetry_queue_exit.h"
#include "tooling/pe_imports.h"
#include "ui/to_disk_menu.h"
#include "tooling/startup_ready.h"
#include "tooling/network_kill_switch.h"
#include "tooling/update_blocker.h"
#include "tooling/update_url_patch.h"

// Spotify-local proxy. All version APIs go to the original System32 DLL.
// The connectivity repair and optional native history stay inside Spotify.
static SRWLOCK hook_lock = SRWLOCK_INIT;
static INIT_ONCE version_once = INIT_ONCE_STATIC_INIT;
static HMODULE real_version;
static HMODULE proxy_module;
static hooks::InitController connectivity_init;

static void Log(const char* message) {
    history::QueueDiagnostic(message);
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
    const DWORD observed_flags=SUCCEEDED(result)&&out?static_cast<DWORD>(*out):0;
    if (SUCCEEDED(result) && out && !(*out & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET))) {
        GUID adapter;
        if (SUCCEEDED(self->GetAdapterId(&adapter))) {
            DWORD flags = SyntheticAdapterConnectivity(adapter);
            if (flags) {
                *out = static_cast<NLM_CONNECTIVITY>(observed_flags | flags);
            }
        }
    }
    if(history::DebugLoggingEnabled()) {
        const bool valid=SUCCEEDED(result)&&out;
        history::RecordConnectivity({true,valid,static_cast<std::int32_t>(result),
            observed_flags,valid?static_cast<DWORD>(*out):0,GetTickCount64()});
    }
    return result;
}

static HRESULT WINAPI RepairCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID* out) {
    HRESULT result = CoCreateInstance(clsid, outer, context, iid, out);
    if (SUCCEEDED(result) && out && *out && IsEqualCLSID(clsid, CLSID_NetworkListManager) &&
        IsEqualIID(iid, IID_INetworkListManager)) {
        if(!HookTable(*out,Kind::Manager))history::LogEvent(history::LogKind::Warning,"warning","NetworkListManager compatibility hook could not be installed");
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

static void StartConnectivityHook(HMODULE module,bool enabled) {
    // Restart-only: disabled startup performs no import discovery, IAT/vtable
    // writes or retry work. Never try to detach live COM hooks during playback.
    if(!enabled)return;
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
            history::LogEvent(history::LogKind::Warning,"warning","connectivity import hook unavailable: CoCreateInstance import not found or PE unsupported");
            connectivity_init.MarkUnsupported();
            break;
        case ImportHookResult::RetryableFailure:
            history::LogEvent(history::LogKind::Warning,"warning","connectivity import hook failed temporarily; retry scheduled");
            connectivity_init.Retry(now);
            break;
    }
}

static void DrainProcessLaunchStatus() {
    const auto status=privacy::GetCrashStatus();
    static privacy::CrashStatus previous{};static bool seen=false;
    if(seen&&status.requested==previous.requested&&status.hook_active==previous.hook_active&&
       status.sanitized_launches==previous.sanitized_launches&&status.launches_without_url==previous.launches_without_url&&
       status.rewrite_failures==previous.rewrite_failures&&status.unmatched_launches==previous.unmatched_launches&&
       status.child_loading==previous.child_loading&&status.children_loaded==previous.children_loaded&&
       status.child_failures==previous.child_failures&&status.unsupported_debug_launches==previous.unsupported_debug_launches)return;
    previous=status;seen=true;
    char line[480];
    std::snprintf(line,sizeof(line),"process launch hook=%u crash_requested=%u uploads_disabled=%llu already_without_url=%llu rewrite_failures=%llu unmatched_launches=%llu child_loading=%u children_loaded=%llu child_failures=%llu unsupported_debug=%llu coverage=future-CreateProcessW-launches-only",
      unsigned(status.hook_active),unsigned(status.requested),static_cast<unsigned long long>(status.sanitized_launches),
      static_cast<unsigned long long>(status.launches_without_url),static_cast<unsigned long long>(status.rewrite_failures),
      static_cast<unsigned long long>(status.unmatched_launches),unsigned(status.child_loading),
      static_cast<unsigned long long>(status.children_loaded),static_cast<unsigned long long>(status.child_failures),
      static_cast<unsigned long long>(status.unsupported_debug_launches));
    Log(line);
}

// Helpers do not initialize the settings writer or logger. Report setup failure
// directly, without creating another owner of SpotifyHistory.ini.
static void LogHelperNetworkFailure() noexcept {
    OutputDebugStringW(L"Floggfy: Network Kill Switch hooks unavailable in Spotify helper; coverage is partial.\n");
    try {
        wchar_t module[32768]{};
        const DWORD length=GetModuleFileNameW(proxy_module,module,32768);
        if(!length||length>=32768)return;
        std::wstring ini=module;const auto slash=ini.find_last_of(L'\\');
        if(slash==std::wstring::npos)return;
        ini.resize(slash+1);ini+=L"SpotifyHistory.ini";
        const int legacy_log=GetPrivateProfileIntW(L"To Disk",L"LogLevel",1,ini.c_str());
        if(GetPrivateProfileIntW(L"Debug",L"LogLevel",legacy_log,ini.c_str())==0)return;
        wchar_t raw[2048]{},expanded[4096]{};
        GetPrivateProfileStringW(L"To Disk",L"Save Location",L"",raw,2048,ini.c_str());
        std::wstring root;
        if(raw[0]) {
            const DWORD size=ExpandEnvironmentStringsW(raw,expanded,4096);
            if(!size||size>4096)return;
            root=expanded;
        } else {
            if(FAILED(history::ReadKnownFolder(FOLDERID_Music,root)))return;
            root+=L"\\Spotify";
        }
        SYSTEMTIME t{};GetSystemTime(&t);char line[320];
        std::snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02uZ warning pid=%lu Network Kill Switch hooks unavailable in Spotify helper; coverage is partial\r\n",
            t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
        if(history::EnsureDirectory(root))
            for(unsigned attempt=0;attempt<3;++attempt) {
                if(history::WriteCappedLog(root+L"\\Floggfy.log",line))break;
                Sleep(20);
            }
    } catch(...) {/* A failed diagnostic must not terminate a Spotify helper. */}
}

static DWORD WINAPI StartupMonitor(LPVOID) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = length && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
    // The exported restart worker runs outside Spotify and owns no app hooks.
    if(name&&!_wcsicmp(name+1,L"rundll32.exe"))return 0;
    const bool spotify_main = name && !_wcsicmp(name + 1, L"Spotify.exe") && !wcsstr(GetCommandLineW(), L"--type=");
    if (!spotify_main) {
        // Every recursively loaded child has independent Winsock state.
        if(!network::Initialize(proxy_module,false,-1))LogHelperNetworkFailure();
        if(privacy::PrepareCrash(false,proxy_module))privacy::FinalizeCrash(MH_ApplyQueued()==MH_OK);
        startup::SignalReady();
        return 0;
    }

    history::InitSettings(proxy_module);
    const auto startup_settings=privacy::EffectiveOptions(history::GetSettings());
    if(startup_settings.network_kill_switch_telemetry_lock&&startup_settings.network_kill_switch==1)
        history::LogEvent(history::LogKind::Activity,"privacy-lock","Network Kill Switch forces all supported telemetry blocking for this session; saved Telemetry preferences preserved");
    if(startup_settings.block_telemetry&&startup_settings.block_queued_telemetry) {
        if(!privacy::InstallRoamingQueueExitCleanup(true,startup_settings.root+L"\\Floggfy.log",startup_settings.log_level))
            history::LogEvent(history::LogKind::Warning,"privacy-queue","Normal-exit queue cleanup hook unavailable; startup cleanup remains active.");
        const auto cleanup=privacy::ClearRoamingTrackingQueues(true);
        char line[320];
        std::snprintf(line,sizeof(line),"Queued telemetry startup cleanup: removed=%u bytes=%llu failed=%u skipped=%u win32_error=%lu; known pending-tracking.pb files only; in-memory queues and other network stacks remain unverified",
            cleanup.removed,static_cast<unsigned long long>(cleanup.bytes),cleanup.failed,cleanup.skipped,cleanup.last_error);
        history::LogEvent(cleanup.failed||cleanup.skipped?history::LogKind::Warning:history::LogKind::Activity,"privacy-queue",line);
    }
    history::ConfigureMarkedCatalog(startup_settings.root,startup_settings.marked_when_downloaded);
    if(!updates::Initialize(startup_settings.block_updates)) {
        char line[180];
        std::snprintf(line,sizeof(line),"Block Updates startup validation failed; requested=%u actual=%u win32_error=%lu",
            unsigned(startup_settings.block_updates),unsigned(updates::Blocked()),updates::LastError());
        history::LogEvent(history::LogKind::Warning,"warning",line);
    } else {
        Log(updates::Blocked() ? "BlockUpdates=1; Update folder read/write deny verified" :
                                "BlockUpdates=0; Update folder blocking disabled");
    }
    if(!network::Initialize(proxy_module,true,startup_settings.network_kill_switch))
        history::LogEvent(history::LogKind::Warning,"warning","Network Kill Switch hooks unavailable; network blocking is not active in this process.");
    else
        Log(startup_settings.network_kill_switch<0 ? "NetworkKillSwitch=-1; network kill hooks disabled" :
            network::Enabled() ? "NetworkKillSwitch=1; Winsock hooks active in this process; queued I/O and uninjected processes are outside coverage" :
                                 "NetworkKillSwitch=0; Winsock hooks ready, networking allowed");
    // Attach this independent launch guard as early as the settings allow.
    // Do not wait for CEF: it cannot cover the separate crash reporter process.
    if(privacy::PrepareCrash(startup_settings.block_telemetry&&startup_settings.block_crash_reports,
                             proxy_module,startup_settings.hook_all_sub_processes))
        privacy::FinalizeCrash(MH_ApplyQueued()==MH_OK);
    const bool network_patch_enabled=startup_settings.network_patch;
    const bool decoded_messages_enabled=startup_settings.debug_networking;
    bool privacy_started=false;
    HMODULE update_url_module = nullptr;
    const char* update_url = nullptr;
    Log(network_patch_enabled?"NetworkPatch=1; legacy network compatibility patch enabled":
        "NetworkPatch=0; network compatibility hooks disabled");
    // Check for late module loads and retry temporary hook failures without
    // delaying the application entry point.
    for (unsigned i = 0;; ++i) {
        HMODULE module = nullptr;
        const bool spotify_loaded = GetModuleHandleExW(0, L"Spotify.dll", &module) != FALSE;
        if (startup_settings.block_updates &&
            (module != update_url_module || (update_url && !updates::UpdateUrlStillRedirected(update_url)))) {
            update_url_module = module;
            update_url = spotify_loaded ? updates::RedirectUpdateUrl(module) : nullptr;
            if (spotify_loaded) {
                if (update_url)
                    Log("Block Updates URL redirect active");
                else
                    history::LogEvent(history::LogKind::Warning,"warning","Block Updates URL redirect unavailable for this Spotify.dll.");
            }
        }
        HMODULE cef;
        if(GetModuleHandleExW(0,L"libcef.dll",&cef)) {
            // Prepare every root before the shared suspension/resume so a
            // separate privacy enable cannot race browser observer attachment.
            history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::StartupHooks);
            bool privacy_prepared=false;
            if(!privacy_started){privacy_prepared=privacy::Prepare(cef,startup_settings.block_telemetry);privacy_started=true;}
            const bool metadata_prepared=history::PrepareMetadataCollector(cef);
            const bool menu_prepared=PrepareToDiskMenu(cef);
            if(privacy_prepared||metadata_prepared||menu_prepared){
                const bool applied=MH_ApplyQueued()==MH_OK;
                if(privacy_prepared)privacy::Finalize(applied);
                if(metadata_prepared)history::FinalizeMetadataCollector(applied);
                if(menu_prepared)FinalizeToDiskMenu(applied);
            }
            FreeLibrary(cef);
        }
        if (spotify_loaded) {
            history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::StartupHooks);
            StartConnectivityHook(module,network_patch_enabled);
            if (i >= 80) {
                StartAudioHistory(module, proxy_module);
                history::StartDecodedConnectMessages(module,decoded_messages_enabled);
            }
            FreeLibrary(module);
        }
        history::DrainDecodedConnectMessages();
        privacy::DrainStatus();
        DrainProcessLaunchStatus();
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
