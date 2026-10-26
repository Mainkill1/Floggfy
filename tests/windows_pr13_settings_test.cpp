// Isolated INI and in-memory configuration only; no Spotify/network operations.
#include "../native/history_settings.cpp"
#include "../native/async_log.cpp"
#include "../native/collector_config.h"
#include <cassert>

static void StrictOptIn() {
    using namespace history;
    for(const wchar_t* value:{L"0",L"2",L"-1",L"01",L"+1",L"true",L"1junk",L"1.0",L"0x1",L"\"1\"",L"",L"111111111111111111111111111111111"}) {
        std::wstring text=L"[To Disk]\r\n";
        for(const auto& flag:restart_flags)text+=std::wstring(flag.key)+L"="+value+L"\r\n";
        Settings parsed;assert(!ReadRestartOptionsText(parsed,text));
        assert(!parsed.network_patch&&!parsed.debug_connect&&!parsed.debug_networking&&!parsed.debug_hook_diagnostics);
    }
    Settings parsed;
    assert(!ReadRestartOptionsText(parsed,L"[tO dIsK]\nNetworkPatch= 1 \nDebugConnect=1\nDebugNetworking=\t1\t\nDebugHookDiagnostics=1\n"));
    assert(parsed.network_patch&&parsed.debug_connect&&parsed.debug_networking&&parsed.debug_hook_diagnostics);
    assert(ReadRestartOptionsText(parsed,L"[Other]\nNetworkPatch=1\n"));
    assert(!parsed.network_patch&&parsed.debug_connect&&!parsed.debug_networking&&!parsed.debug_hook_diagnostics);
    assert(ReadRestartOptionsText(parsed,L"[To Disk]\nNetworkPatch=1\nNetworkPatch=0\n"));
    assert(!parsed.network_patch); // Ambiguous duplicate opt-ins fail closed.
}
static void EffectiveDebug() {
    using namespace history;
    for(unsigned bits=0;bits<4;++bits) {
        const bool log=(bits&1)!=0,debug=(bits&2)!=0,effective=log&&debug;
        InterlockedExchange(&log_enabled,log);InterlockedExchange(&debug_enabled,debug);
        InterlockedExchange(&networking_enabled,1);InterlockedExchange(&hook_diagnostics_enabled,1);
        assert(DebugLoggingEnabled()==effective&&NetworkingLoggingEnabled()==effective&&HookDiagnosticsEnabled()==effective);
        Settings config;config.log=log;config.debug_log=debug;config.debug_networking=true;
        const auto options=BuildCollectorOptions(config);
        assert(options.find(effective?L"debug:true":L"debug:false")!=std::wstring::npos);
        assert(options.find(effective?L"connect:true":L"connect:false")!=std::wstring::npos);
        assert(options.find(effective?L"networking:true":L"networking:false")!=std::wstring::npos);
    }
    InterlockedExchange(&log_enabled,0);InterlockedExchange(&debug_enabled,1);
    signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(signal);
    QueueDiagnostic("disabled debug");QueueNetworkingDiagnostic("disabled networking");
    assert(activity_queue.count==0&&network_queue.count==0);
    CloseHandle(signal);signal=nullptr;
}
static void PreserveExternalEdits() {
    using namespace history;
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,path=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(CreateDefaultIni(path));
    assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugLog",L"1",path.c_str()));
    for(const auto& flag:restart_flags)assert(WritePrivateProfileStringW(L"To Disk",flag.key,nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"Unknown",L"Keep",L"\u97f3\u697d",path.c_str()));
    InitSettings(nullptr);assert(FlushSettings(5000));
    assert(!GetSettings().network_patch&&GetSettings().debug_connect&&!DebugLoggingEnabled());
    for(const auto& flag:restart_flags)
        assert(GetPrivateProfileIntW(L"To Disk",flag.key,9,path.c_str())==unsigned(flag.default_value));
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnect",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugHookDiagnostics",L"1",path.c_str()));
    const auto epoch=CaptureEpoch();assert(SetLyrics(false)&&FlushSettings(5000)&&CaptureEpoch()==epoch);
    assert(GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugConnect",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugHookDiagnostics",9,path.c_str())==1);
    assert(!GetSettings().network_patch&&!GetSettings().debug_networking); // Remains restart-only.
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"-1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"2",path.c_str()));
    assert(SetDownloads(false)&&FlushSettings(5000));
    std::wstring text;bool unicode=false;assert(settings_file::Read(path,text,unicode)&&unicode);
    assert(settings_file::Find(text,L"NetworkPatch").value==L"-1");
    assert(settings_file::Find(text,L"DebugNetworking").value==L"2");
    Settings parsed;assert(!ReadRestartOptions(parsed,path)&&!parsed.network_patch&&!parsed.debug_networking);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Unknown",L"Keep",L"",kept,16,path.c_str())==2);
    assert(std::wstring(kept)==L"\u97f3\u697d");
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",nullptr,path.c_str()));
    {ExclusiveSrw guard(lock);settings.debug_networking=true;}
    assert(SetLyrics(true)&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,path.c_str())==0); // Missing => default, not stale snapshot.
}
int wmain(){StrictOptIn();EffectiveDebug();PreserveExternalEdits();puts("PASS: external startup edits, default migration, strict opt-in, Unicode, effective native/renderer debug opt-out");return 0;}
