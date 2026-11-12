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
        assert(!parsed.save_unmatched&&!parsed.network_patch&&!parsed.debug_connect&&!parsed.debug_networking&&!parsed.debug_hook_diagnostics&&!parsed.dump_streams);
    }
    Settings parsed;
    assert(!ReadRestartOptionsText(parsed,L"[tO dIsK]\nSaveUnmatched=1\nNetworkPatch= 1 \nDebugConnect=1\nDebugNetworking=\t1\t\nDebugHookDiagnostics=1\nDumpStreams=1\n"));
    assert(parsed.network_patch&&parsed.debug_connect&&parsed.debug_networking&&parsed.debug_hook_diagnostics);
    assert(ReadRestartOptionsText(parsed,L"[Other]\nNetworkPatch=1\n"));
    assert(!parsed.network_patch&&parsed.debug_connect&&!parsed.debug_networking&&!parsed.debug_hook_diagnostics);
    assert(ReadRestartOptionsText(parsed,L"[To Disk]\nNetworkPatch=1\nNetworkPatch=0\n"));
    assert(!parsed.network_patch); // Ambiguous duplicate opt-ins fail closed.
}
static void MatcherOptions() {
    using namespace history;
    const Settings defaults;
    Settings parsed;parsed.matcher_time_offset_ms=77;parsed.matcher_duration_ms=999;
    assert(ReadMatcherOptionsText(parsed,L"[To Disk]\nTimeOffsetToleranceMs=5\nMaxMatchQueue=9\nDurationToleranceMs=50\n"));
    for(const auto& option:matcher_options)assert(parsed.*(option.member)==defaults.*(option.member));
    struct Values {unsigned queue,offset,duration;};
    for(const auto& values:std::array<Values,4>{{{1,0,0},{9,7000,125},{100,120000,2000},{5,10000,250}}}) {
        const auto text=L"[mAtChEr]\n MaxMatchQueue = "+std::to_wstring(values.queue)+L"\n timeoffsettolerancems = \t"+std::to_wstring(values.offset)+
            L" \nDurationToleranceMs="+std::to_wstring(values.duration)+L"\n[Other]\nTimeOffsetToleranceMs=99\nDurationToleranceMs=999\n";
        assert(!ReadMatcherOptionsText(parsed,text));
        assert(parsed.matcher_max_queue==values.queue&&parsed.matcher_time_offset_ms==values.offset&&parsed.matcher_duration_ms==values.duration);
    }
    for(const wchar_t* value:{L"",L"-1",L"+1",L"1.5",L"true",L"1junk",L"0x1",L"\"1\"",L"99999999999999999999999999999999999"}) {
        std::wstring text=L"[Matcher]\n";
        for(const auto& option:matcher_options)text+=std::wstring(option.key)+L"="+value+L"\n";
        assert(ReadMatcherOptionsText(parsed,text));
        for(const auto& option:matcher_options)assert(parsed.*(option.member)==option.initial);
    }
    assert(ReadMatcherOptionsText(parsed,L"[Matcher]\nMaxMatchQueue=101\nTimeOffsetToleranceMs=120001\nDurationToleranceMs=2001\n"));
    for(const auto& option:matcher_options)assert(parsed.*(option.member)==option.initial);
    assert(ReadMatcherOptionsText(parsed,L"[Matcher]\nMaxMatchQueue=0\nTimeOffsetToleranceMs=0\nDurationToleranceMs=0\n"));
    assert(parsed.matcher_max_queue==5&&parsed.matcher_time_offset_ms==0&&parsed.matcher_duration_ms==0);
    for(const auto& duplicate:matcher_options) {
        std::wstring text=L"[Matcher]\n";
        for(const auto& option:matcher_options)text+=std::wstring(option.key)+L"="+std::to_wstring(option.minimum)+L"\n";
        text+=std::wstring(L"[Matcher]\n")+duplicate.key+L"="+std::to_wstring(duplicate.minimum)+L"\n";
        assert(ReadMatcherOptionsText(parsed,text));
        for(const auto& option:matcher_options)assert(parsed.*(option.member)==(option.member==duplicate.member?option.initial:option.minimum));
    }
    for(const unsigned seconds:{0u,3u,10u,120u}) {
        const auto text=L"[Matcher]\nTimingToleranceSeconds="+std::to_wstring(seconds)+L"\n";
        assert(ReadMatcherOptionsText(parsed,text));
        assert(parsed.matcher_time_offset_ms==seconds*1000);
        auto migrated=text;InsertMissingRestartDefaults(migrated);
        assert(!settings_file::Find(migrated,L"TimingToleranceSeconds",L"[Matcher]").present);
        assert(!ReadMatcherOptionsText(parsed,migrated)&&parsed.matcher_time_offset_ms==seconds*1000);
    }
    for(const wchar_t* legacy:{L"121",L"-1",L"invalid",L"3\nTimingToleranceSeconds=3"}) {
        assert(ReadMatcherOptionsText(parsed,std::wstring(L"[Matcher]\nTimingToleranceSeconds=")+legacy+L"\n"));
        assert(parsed.matcher_time_offset_ms==10000);
    }
    std::wstring migrated=L"; retain comment\n[To Disk]\nTimingToleranceSeconds=88\nSaveUnmatched=0\n[Matcher]\nTimingToleranceSeconds=3\nTimeOffsetToleranceMs=7000\n[Unknown]\nTimingToleranceSeconds=88\nDurationToleranceMs=88\n";
    InsertMissingRestartDefaults(migrated);
    assert(settings_file::Find(migrated,L"TimingToleranceSeconds").value==L"88");
    assert(settings_file::Find(migrated,L"SaveUnmatched").value==L"0");
    assert(!settings_file::Find(migrated,L"TimingToleranceSeconds",L"[Matcher]").present);
    assert(settings_file::Find(migrated,L"TimeOffsetToleranceMs",L"[Matcher]").value==L"7000");
    assert(settings_file::Find(migrated,L"TimingToleranceSeconds",L"[Unknown]").value==L"88");
    assert(settings_file::Find(migrated,L"DurationToleranceMs",L"[Matcher]").value==L"250");
    assert(settings_file::Find(migrated,L"DurationToleranceMs",L"[Unknown]").value==L"88");
    const auto once=migrated;InsertMissingRestartDefaults(migrated);assert(migrated==once);
    settings_file::Set(migrated,L"TimeOffsetToleranceMs",L"0",L"[Matcher]");
    assert(settings_file::Find(migrated,L"TimeOffsetToleranceMs",L"[Matcher]").value==L"0");
    assert(settings_file::Find(migrated,L"TimingToleranceSeconds").value==L"88");
    std::wstring absent=L"[Unknown]\nKeep=original";InsertMissingRestartDefaults(absent);
    assert(!ReadMatcherOptionsText(parsed,absent));
    for(const auto& option:matcher_options)assert(parsed.*(option.member)==option.initial);
    assert(settings_file::Find(absent,L"Keep",L"[Unknown]").value==L"original");
    std::wstring invalid=L"[To Disk]\nSaveUnmatched=0\n[Matcher]\nTimingToleranceSeconds=3\nTimeOffsetToleranceMs=invalid\nDurationToleranceMs=1\nDurationToleranceMs=2\n";
    InsertMissingRestartDefaults(invalid);
    assert(settings_file::Find(invalid,L"TimeOffsetToleranceMs",L"[Matcher]").value==L"invalid");
    assert(settings_file::Find(invalid,L"DurationToleranceMs",L"[Matcher]").duplicate);
    assert(ReadMatcherOptionsText(parsed,invalid)&&parsed.matcher_time_offset_ms==10000&&parsed.matcher_duration_ms==250);
    std::wstring embedded;for(const char byte:std::string(default_ini))embedded+=wchar_t(static_cast<unsigned char>(byte));
    assert(!ReadMatcherOptionsText(parsed,embedded));
    for(const auto& option:matcher_options)assert(parsed.*(option.member)==defaults.*(option.member));
}
static void EffectiveDebug() {
    using namespace history;
    for(const unsigned level:{0u,1u,2u,4u}) {
        const bool effective=level>=4;
        InterlockedExchange(&log_level_value,LONG(level));
        InterlockedExchange(&networking_enabled,1);InterlockedExchange(&hook_diagnostics_enabled,1);
        assert(DebugLoggingEnabled()==effective&&NetworkingLoggingEnabled()==effective&&HookDiagnosticsEnabled()==effective);
        Settings config;config.log_level=level;config.debug_networking=true;config.debug_hook_diagnostics=true;
        const auto options=BuildCollectorOptions(config);
        assert(options.find(effective?L"debug:true":L"debug:false")!=std::wstring::npos);
        assert(options.find(effective?L"connect:true":L"connect:false")!=std::wstring::npos);
        assert(options.find(effective?L"networking:true":L"networking:false")!=std::wstring::npos);
    }
    InterlockedExchange(&log_level_value,4);InterlockedExchange(&networking_enabled,0);
    InterlockedExchange(&hook_diagnostics_enabled,1);
    assert(!NetworkingLoggingEnabled()&&HookDiagnosticsEnabled());
    InterlockedExchange(&networking_enabled,1);InterlockedExchange(&hook_diagnostics_enabled,0);
    assert(NetworkingLoggingEnabled()&&!HookDiagnosticsEnabled());
    InterlockedExchange(&log_level_value,0);
    signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(signal);
    Settings opted_out;opted_out.log_level=4;opted_out.debug_networking=true;opted_out.debug_connect=false;
    assert(BuildCollectorOptions(opted_out).find(L",connect:false,")!=std::wstring::npos);
    for(int value:{-1,0,1}) {
        Settings config;config.never_pause=value;config.metadata=false;config.downloads=false;
        assert(BuildCollectorOptions(config).find(value==1?L"neverPause:true":L"neverPause:false")!=std::wstring::npos);
    }
    Settings auto_skip;auto_skip.auto_skip_downloaded=true;auto_skip.generation=9;
    AutoSkipRequest request{7,9,"spotify:track:0000000000000000000001","playback-1"};
    const auto requested=BuildCollectorOptions(auto_skip,request);
    assert(requested.find(L"autoSkip:true")!=std::wstring::npos);
    assert(requested.find(L"skipToken:7")!=std::wstring::npos);
    assert(requested.find(L"skipUri:'spotify:track:0000000000000000000001'")!=std::wstring::npos);
    assert(requested.find(L"skipInstance:'playback-1'")!=std::wstring::npos);
    request.settings_generation=8;
    assert(BuildCollectorOptions(auto_skip,request).find(L"skipToken:")==std::wstring::npos);
    auto_skip.auto_skip_downloaded=false;request.settings_generation=9;
    assert(BuildCollectorOptions(auto_skip,request).find(L"skipToken:")==std::wstring::npos);
    QueueDiagnostic("disabled debug");QueueNetworkingDiagnostic("disabled networking");
    assert(activity_queue.count==0&&diagnostic_queue.count==0);
    CloseHandle(signal);signal=nullptr;
}
static void PreserveExternalEdits() {
    using namespace history;
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,path=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(CreateDefaultIni(path));
    assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugLog",L"1",path.c_str()));
    for(const auto& flag:restart_flags)assert(WritePrivateProfileStringW(L"To Disk",flag.key,nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"TimingToleranceSeconds",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"TimeOffsetToleranceMs",nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"DurationToleranceMs",nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"Unknown",L"Keep",L"\u97f3\u697d",path.c_str()));
    InitSettings(nullptr);assert(FlushSettings(5000));
    assert(GetSettings().matcher_max_queue==5&&GetSettings().matcher_time_offset_ms==0&&GetSettings().matcher_duration_ms==250&&GetSettings().save_unmatched);
    assert(GetPrivateProfileIntW(L"Matcher",L"TimeOffsetToleranceMs",999,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"Matcher",L"TimingToleranceSeconds",999,path.c_str())==999);
    assert(GetPrivateProfileIntW(L"Matcher",L"DurationToleranceMs",999,path.c_str())==250);
    assert(!GetSettings().network_patch&&GetSettings().debug_connect&&!DebugLoggingEnabled()&&LogLevel()==1);
    for(const auto& flag:restart_flags)
        assert(GetPrivateProfileIntW(L"To Disk",flag.key,9,path.c_str())==unsigned(flag.default_value));
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnect",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugHookDiagnostics",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"SaveUnmatched",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"MaxMatchQueue",L"9",path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"TimeOffsetToleranceMs",L"7000",path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"DurationToleranceMs",L"125",path.c_str()));
    const auto epoch=CaptureEpoch();assert(SetLyrics(false)&&FlushSettings(5000)&&CaptureEpoch()==epoch);
    assert(GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugConnect",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugHookDiagnostics",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"SaveUnmatched",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"Matcher",L"MaxMatchQueue",999,path.c_str())==9);
    assert(GetPrivateProfileIntW(L"Matcher",L"TimeOffsetToleranceMs",999,path.c_str())==7000);
    assert(GetPrivateProfileIntW(L"Matcher",L"DurationToleranceMs",999,path.c_str())==125);
    assert(GetSettings().matcher_max_queue==5&&GetSettings().matcher_time_offset_ms==0&&GetSettings().matcher_duration_ms==250&&GetSettings().save_unmatched);
    Settings matcher_reloaded;assert(!ReadMatcherOptions(matcher_reloaded,path));
    assert(matcher_reloaded.matcher_max_queue==9&&matcher_reloaded.matcher_time_offset_ms==7000&&matcher_reloaded.matcher_duration_ms==125);
    assert(!GetSettings().network_patch&&!GetSettings().debug_networking); // Remains restart-only.
    assert(WritePrivateProfileStringW(L"To Disk",L"NetworkPatch",L"-1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",L"2",path.c_str()));
    assert(SetDownloads(false)&&FlushSettings(5000));
    std::wstring text;bool unicode=false;assert(settings_file::Read(path,text,unicode)&&unicode);
    assert(settings_file::Find(text,L"NetworkPatch").value==L"-1");
    assert(settings_file::Find(text,L"DebugNetworking").value==L"2");
    assert(settings_file::Find(text,L"TimeOffsetToleranceMs",L"[Matcher]").value==L"7000");
    assert(settings_file::Find(text,L"DurationToleranceMs",L"[Matcher]").value==L"125");
    Settings parsed;assert(!ReadRestartOptions(parsed,path)&&!parsed.network_patch&&!parsed.debug_networking);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Unknown",L"Keep",L"",kept,16,path.c_str())==2);
    assert(std::wstring(kept)==L"\u97f3\u697d");
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",nullptr,path.c_str()));
    {ExclusiveSrw guard(lock);settings.debug_networking=true;}
    assert(SetLyrics(true)&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,path.c_str())==0); // Missing => default, not stale snapshot.
}
int wmain(){StrictOptIn();MatcherOptions();EffectiveDebug();PreserveExternalEdits();puts("PASS: matcher limits/duplicates/sections, external startup edits, default migration, strict opt-in, Unicode, effective native/renderer debug opt-out");return 0;}
