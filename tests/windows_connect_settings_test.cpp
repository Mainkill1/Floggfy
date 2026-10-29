// Isolated temporary INI only; no Spotify process or network operations.
#include "../native/history_settings.cpp"
#include <cassert>
namespace history {
void StartLogger(){}
void QueueDiagnostic(const char*){}
void LogEvent(LogKind,const char*,const std::string&){}
}
int wmain() {
    assert(history::Settings{}.debug_connect&&!history::Settings{}.debug_log);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,ini=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(history::CreateDefaultIni(ini));
    history::Settings parsed;assert(!history::ReadRestartOptions(parsed,ini)&&parsed.debug_connect);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnect",L"0",ini.c_str()));
    assert(!history::ReadRestartOptions(parsed,ini)&&!parsed.debug_connect);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnect",nullptr,ini.c_str()));
    assert(history::ReadRestartOptions(parsed,ini)&&parsed.debug_connect);
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",ini.c_str()));
    history::InitSettings(nullptr);assert(history::GetSettings().debug_connect&&!history::DebugLoggingEnabled());
    assert(history::FlushSettings(5000)&&GetPrivateProfileIntW(L"To Disk",L"DebugConnect",9,ini.c_str())==1);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnect",L"0",ini.c_str()));
    const auto epoch=history::CaptureEpoch();assert(history::SetLyrics(false)&&history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugConnect",9,ini.c_str())==0&&history::CaptureEpoch()==epoch);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,16,ini.c_str())==2&&std::wstring(kept)==L"\u97f3\u697d");
    puts("PASS: DebugConnect defaults, explicit opt-out, missing-key migration, Unicode preservation and capture-independent persistence");
    return 0;
}
