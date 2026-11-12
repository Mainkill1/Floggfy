// Isolated files/settings only. Never loads Spotify or sends network requests.
#include "../native/history_settings.cpp"
// Count actual Win32 I/O in the production capped writer, not elapsed wall time.
static unsigned writes=0,compactions=0,reads=0;
static unsigned long long written_bytes=0,read_bytes=0;
static BOOL WINAPI CountedWriteFile(HANDLE file,LPCVOID data,DWORD size,LPDWORD done,LPOVERLAPPED overlap) {
    ++writes;const auto result=::WriteFile(file,data,size,done,overlap);if(result&&done)written_bytes+=*done;return result;
}
static BOOL WINAPI CountedReadFile(HANDLE file,LPVOID data,DWORD size,LPDWORD done,LPOVERLAPPED overlap) {
    ++reads;const auto result=::ReadFile(file,data,size,done,overlap);if(result&&done)read_bytes+=*done;return result;
}
static BOOL WINAPI CountedSetEndOfFile(HANDLE file) {++compactions;return ::SetEndOfFile(file);}
#define WriteFile CountedWriteFile
#define ReadFile CountedReadFile
#define SetEndOfFile CountedSetEndOfFile
#include "../native/async_log.cpp"
#undef WriteFile
#undef ReadFile
#undef SetEndOfFile
#define main TestCappedLog
#include "windows_log_test.cpp"
#undef main
#include <cassert>

static void ClientPrivacy() {
    std::string summary;
    using history::SummarizeClientNetworking;
    assert(SummarizeClientNetworking("FLOGGFY_STATUS:control request=pause source=local-client",summary));
    assert(summary.find("direction=local")!=std::string::npos);
    assert(summary.find("request=pause")!=std::string::npos);
    assert(SummarizeClientNetworking("FLOGGFY_STATUS:connect state=connected target=local device_changed=1 controller=unknown",summary));
    assert(SummarizeClientNetworking("FLOGGFY_STATUS:client obs paused=1 buf=0 active=? code=-7 status=503 inst=0 dev=?",summary));
    assert(SummarizeClientNetworking("FLOGGFY_STATUS:terminal item=null ctx=missing uri=0 inst=0 paused=1 buf=1 local=1 mon=1",summary));
    assert(summary.find("source=terminal-state")!=std::string::npos);
    assert(!SummarizeClientNetworking("FLOGGFY_STATUS:terminal item=PRIVATE_URI ctx=missing uri=0 inst=0 paused=1 buf=1 local=1 mon=1",summary));
    assert(!SummarizeClientNetworking("FLOGGFY_STATUS:terminal item=null ctx=missing uri=0 inst=0 paused=1 buf=1 local=1 mon=1 SECRET",summary));
    {
        assert(!history::signal);history::signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(history::signal);
        const auto old_level=history::LogLevel();InterlockedExchange(&history::log_level_value,4);
        const std::wstring endpoint=L"FLOGGFY_STATUS:terminal item=null ctx=missing uri=0 inst=0 paused=1 buf=1 local=1 mon=1";
        assert(history::QueueRendererDiagnostic(endpoint));history::Record record;
        assert(history::Take(history::diagnostic_queue,record)&&"terminal observations must survive the actual renderer gateway size bound");
        assert(history::QueueRendererDiagnostic(L"FLOGGFY_STATUS:terminal-clock pos_ms=122000 length_ms=122000 ctx_uri=? age_ms=100"));
        assert(history::Take(history::diagnostic_queue,record)&&std::string(record.detail).find("source=terminal-clock")!=std::string::npos);
        assert(history::QueueRendererDiagnostic(endpoint+std::wstring(120,L'x')));
        assert(!history::Take(history::diagnostic_queue,record)&&"oversized renderer observations remain rejected");
        InterlockedExchange(&history::log_level_value,LONG(old_level));
        CloseHandle(history::signal);history::signal=nullptr;
    }
    assert(SummarizeClientNetworking("FLOGGFY_NETWORK_V1:request op=7 kind=lyrics phase=response code=200 elapsed_ms=12",summary));
    assert(summary.find("service=lyrics")!=std::string::npos&&summary.find("direction=in")!=std::string::npos);
    for(const char* message:{
        "FLOGGFY_STATUS:control request=PRIVATE_TOKEN source=local-client",
        "FLOGGFY_STATUS:control request=pause source=PRIVATE_DEVICE",
        "FLOGGFY_STATUS:connect state=connected target=local device_changed=1 controller=unknown token=SECRET",
        "FLOGGFY_NETWORK_V1:request op=7 kind=lyrics phase=response code=PRIVATE_TOKEN elapsed_ms=12",
        "FLOGGFY_NETWORK_V1:request op=7 kind=media phase=response code=200 elapsed_ms=12",
        "FLOGGFY_NETWORK_V1:request op=7 kind=lyrics phase=response code=200 elapsed_ms=12\nSECRET",
        "FLOGGFY_STATUS:client obs paused=1 buf=0 active=? code=999999999999999999999 status=503 inst=0 dev=?"}) {
        assert(!SummarizeClientNetworking(message,summary)&&summary.empty());
    }
}
static std::string ReadLog(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size{};assert(GetFileSizeEx(file,&size));std::string contents(size_t(size.QuadPart),'\0');
    DWORD read=0;assert(::ReadFile(file,contents.data(),DWORD(contents.size()),&read,nullptr)&&read==contents.size());
    CloseHandle(file);return contents;
}
static void ClearQueues() {
    history::Record record{};while(history::Take(history::activity_queue,record)){}while(history::Take(history::diagnostic_queue,record)){}
    while(history::Take(history::networking_queue,record)){}while(history::Take(history::hook_queue,record)){}
    history::activity_queue.omitted.store(0);history::diagnostic_queue.omitted.store(0);
    history::networking_queue.omitted.store(0);history::hook_queue.omitted.store(0);
}
static void LevelMatrix() {
    using namespace history;
    struct Expected {unsigned level,activity,diagnostic;};
    for(const auto expected:{Expected{0,0,0},Expected{1,3,0},Expected{2,4,0},Expected{4,4,1}}) {
        InterlockedExchange(&log_level_value,LONG(expected.level));settings.log_level=expected.level;
        LogEvent(LogKind::Finished,"finished","started failed misleading title");
        LogEvent(LogKind::Activity,"activity","finished failed misleading title");
        LogEvent(LogKind::Warning,"ignored","finished misleading warning");
        LogEvent(LogKind::Failure,"ignored","finished misleading failure");
        LogEvent(LogKind::Diagnostic,"debug","finished misleading diagnostic");
        assert(activity_queue.count==expected.activity&&diagnostic_queue.count==expected.diagnostic);
        if(expected.level==1) {
            Record records[4]{};assert(TakeBatch(activity_queue,records,4)==3);
            assert(std::string(records[0].event)=="finished");
            assert(std::string(records[1].event)=="warning");
            assert(std::string(records[2].event)=="failed");
        }
        ClearQueues();
    }
    activity_queue.sequence=0;diagnostic_queue.sequence=0;networking_queue.sequence=0;hook_queue.sequence=0;
}
static void LocalTimestamp() {
    using namespace history;
    Record record{};record.utc.wYear=2024;record.utc.wMonth=1;record.utc.wDay=15;
    record.utc.wHour=12;record.utc.wMinute=34;record.utc.wSecond=56;record.utc.wMilliseconds=321;
    record.observed_ms=99;record.sequence=7;strcpy(record.event,"finished");strcpy(record.detail,"local timestamp");
    SYSTEMTIME local{};assert(SystemTimeToTzSpecificLocalTime(nullptr,&record.utc,&local));
    FILETIME utc_file{},local_file{};assert(SystemTimeToFileTime(&record.utc,&utc_file));
    assert(SystemTimeToFileTime(&local,&local_file));
    ULARGE_INTEGER utc_ticks{},local_ticks{};utc_ticks.LowPart=utc_file.dwLowDateTime;utc_ticks.HighPart=utc_file.dwHighDateTime;
    local_ticks.LowPart=local_file.dwLowDateTime;local_ticks.HighPart=local_file.dwHighDateTime;
    const auto delta=LONGLONG(local_ticks.QuadPart)-LONGLONG(utc_ticks.QuadPart);
    const auto offset_minutes=delta/600000000LL;const auto magnitude=offset_minutes<0?-offset_minutes:offset_minutes;
    char normal_prefix[80],diagnostic_prefix[80];
    std::snprintf(normal_prefix,sizeof(normal_prefix),"%04u-%02u-%02uT%02u:%02u:%02u%c%02lld:%02lld ",
        local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute,local.wSecond,
        offset_minutes<0?'-':'+',magnitude/60,magnitude%60);
    std::snprintf(diagnostic_prefix,sizeof(diagnostic_prefix),"%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02lld:%02lld ",
        local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute,local.wSecond,local.wMilliseconds,
        offset_minutes<0?'-':'+',magnitude/60,magnitude%60);
    const auto normal=FormatLogRecord(record,false),diagnostic=FormatLogRecord(record,true);
    assert(normal.rfind(normal_prefix,0)==0&&normal.find("Z ")==std::string::npos);
    assert(normal.find(".321")==std::string::npos);
    assert(diagnostic.rfind(diagnostic_prefix,0)==0&&diagnostic.find("Z ")==std::string::npos);
}
static void IndependentLanes(const std::wstring& root) {
    using namespace history;
    const auto activity_path=root+L"\\Floggfy.log";
    const auto debug_path=root+L"\\Floggfy-diagnostics.log";
    const auto network_path=root+L"\\Floggfy-networking.log";
    const auto hook_path=root+L"\\Floggfy-hook-diagnostics.log";
    for(const auto& path:{activity_path,debug_path,network_path,hook_path})DeleteFileW(path.c_str());
    InterlockedExchange(&log_level_value,4);InterlockedExchange(&networking_enabled,1);
    InterlockedExchange(&hook_diagnostics_enabled,1);settings.log_level=4;
    QueueDiagnostic("debug lane marker");
    QueueNetworkingDiagnostic("network lane marker");
    QueueHookDiagnostic("hook lane marker");
    assert(activity_queue.count==0&&diagnostic_queue.count==1&&networking_queue.count==1&&hook_queue.count==1);
    Record record{};assert(Take(diagnostic_queue,record));WriteRecord(record,LogLane::Diagnostic);
    assert(Take(networking_queue,record));WriteRecord(record,LogLane::Networking);
    assert(Take(hook_queue,record));WriteRecord(record,LogLane::Hook);
    const auto debug=ReadLog(debug_path),network=ReadLog(network_path),hook=ReadLog(hook_path);
    assert(debug.find("debug lane marker")!=std::string::npos&&debug.find("network lane marker")==std::string::npos&&debug.find("hook lane marker")==std::string::npos);
    assert(network.find("network lane marker")!=std::string::npos&&network.find("debug lane marker")==std::string::npos&&network.find("hook lane marker")==std::string::npos);
    assert(hook.find("hook lane marker")!=std::string::npos&&hook.find("debug lane marker")==std::string::npos&&hook.find("network lane marker")==std::string::npos);
    for(unsigned i=0;i<128;++i)QueueNetworkingDiagnostic("bounded network diagnostic");
    QueueNetworkingDiagnostic("omitted network diagnostic");QueueDiagnostic("debug remains available");QueueHookDiagnostic("hook remains available");
    assert(networking_queue.count==128&&networking_queue.omitted.load()==1);
    assert(diagnostic_queue.count==1&&diagnostic_queue.omitted.load()==0);
    assert(hook_queue.count==1&&hook_queue.omitted.load()==0);
    ClearQueues();
    InterlockedExchange(&networking_enabled,0);QueueNetworkingDiagnostic("network disabled");QueueHookDiagnostic("hook independent");
    assert(networking_queue.count==0&&hook_queue.count==1);ClearQueues();
    InterlockedExchange(&networking_enabled,1);InterlockedExchange(&hook_diagnostics_enabled,0);
    QueueNetworkingDiagnostic("network independent");QueueHookDiagnostic("hook disabled");
    assert(networking_queue.count==1&&hook_queue.count==0);ClearQueues();
    InterlockedExchange(&log_level_value,2);InterlockedExchange(&hook_diagnostics_enabled,1);
    QueueDiagnostic("debug below level four");QueueNetworkingDiagnostic("network below level four");QueueHookDiagnostic("hook below level four");
    assert(diagnostic_queue.count==0&&networking_queue.count==0&&hook_queue.count==0);
    InterlockedExchange(&log_level_value,4);
    for(const auto& path:{debug_path,network_path,hook_path})assert(DeleteFileW(path.c_str()));
    activity_queue.sequence=0;diagnostic_queue.sequence=0;networking_queue.sequence=0;hook_queue.sequence=0;
    writes=0;
}
static void QueueAndSettings() {
    using namespace history;
    assert(!Settings{}.debug_networking);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,root=exe.substr(0,exe.find_last_of(L'\\'));
    const auto diagnostic_path=root+L"\\Floggfy-diagnostics.log",normal_path=root+L"\\Floggfy.log";
    const auto networking_path=root+L"\\Floggfy-networking.log",hook_path=root+L"\\Floggfy-hook-diagnostics.log";
    DeleteFileW(normal_path.c_str());DeleteFileW(diagnostic_path.c_str());DeleteFileW(networking_path.c_str());DeleteFileW(hook_path.c_str());
    settings.root=root;settings.log_level=4;
    signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(signal);writes=0;
    LevelMatrix();LocalTimestamp();IndependentLanes(root);
    InterlockedExchange(&log_level_value,4);InterlockedExchange(&networking_enabled,0);InterlockedExchange(&hook_diagnostics_enabled,0);
    QueueNetworkingDiagnostic("test disabled");assert(networking_queue.count==0);
    InterlockedExchange(&networking_enabled,1);
    InterlockedExchange(&log_level_value,1);QueueNetworkingDiagnostic("test no debug");assert(networking_queue.count==0);
    InterlockedExchange(&log_level_value,0);
    QueueNetworkingDiagnostic("test no log");assert(networking_queue.count==0);
    // Every reserved renderer message remains claimed even when disabled.
    assert(QueueRendererDiagnostic(L"FLOGGFY_STATUS:PRIVATE_FIELD=SECRET"));
    assert(activity_queue.count==0&&diagnostic_queue.count==0);
    InterlockedExchange(&log_level_value,4);
    QueueDiagnostic("FLOGGFY_STATUS:control request=pause source=local-client");
    assert(diagnostic_queue.count==1&&networking_queue.count==0&&activity_queue.count==0);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=? elapsed_ms=0");
    assert(diagnostic_queue.count==1&&networking_queue.count==1);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=PRIVATE_TOKEN elapsed_ms=0");
    assert(diagnostic_queue.count==1&&networking_queue.count==1&&activity_queue.count==0);
    for(unsigned i=0;i<128;++i)QueueDiagnostic("bounded basic diagnostic");
    assert(diagnostic_queue.count==128&&diagnostic_queue.omitted.load()==1);
    LogEvent(LogKind::Activity,"finished","normal activity still fits");assert(activity_queue.count==1);
    AcquireSRWLockExclusive(&diagnostic_queue.lock);QueueDiagnostic("contention never waits");ReleaseSRWLockExclusive(&diagnostic_queue.lock);
    assert(diagnostic_queue.omitted.load()==2);
    Record record{};assert(Take(diagnostic_queue,record));assert(record.sequence==1&&record.observed_ms>0);
    assert(std::string(record.detail).find("request=pause")!=std::string::npos);
    const auto formatted=FormatLogRecord(record,true);
    assert(formatted.find("pid=")!=std::string::npos&&formatted.find("seq=1")!=std::string::npos);
    assert(formatted.find("mono_ms=")!=std::string::npos&&formatted.find("direction=local")!=std::string::npos);
    assert(writes==0);WriteBatch(&record,1,LogLane::Diagnostic);
    assert(ReadLog(diagnostic_path)==formatted&&ReadLog(normal_path).empty());
    assert(DeleteFileW(diagnostic_path.c_str()));
    ClearQueues();
    // Classification is selected by the producer, never message text. Ordinary
    // successful titles contain every old keyword, and identical real events survive.
    for(const char* keyword:{"failed","discarded","rejected","overflow","unsupported","stopped after"})
        LogEvent(LogKind::Activity,"finished",std::string("saved ")+keyword+".ogg");
    LogEvent(LogKind::Activity,"finished","saved same title.ogg");LogEvent(LogKind::Activity,"finished","saved same title.ogg");
    assert(activity_queue.count==8&&diagnostic_queue.count==0);
    Record records[16]{};const auto count=TakeBatch(activity_queue,records,16);assert(count==8);
    writes=0;WriteBatch(records,count,LogLane::Activity);assert(writes==1);
    auto normal=ReadLog(normal_path);for(const char* keyword:{"failed","discarded","rejected","overflow","unsupported","stopped after"})
        assert(normal.find(std::string("finished saved ")+keyword+".ogg")!=std::string::npos);
    const auto same=normal.find("finished saved same title.ogg");assert(same!=std::string::npos&&normal.find("finished saved same title.ogg",same+1)!=std::string::npos);
    assert(normal.find(" failed ")==std::string::npos);assert(DeleteFileW(normal_path.c_str()));
    for(int debug=0;debug<2;++debug) {
        InterlockedExchange(&log_level_value,debug?4:1);settings.log_level=debug?4:1;
        LogEvent(LogKind::Failure,"ignored","destination inaccessible");
        assert(activity_queue.count==1&&diagnostic_queue.count==0);assert(Take(activity_queue,record));
        WriteBatch(&record,1,LogLane::Activity);normal=ReadLog(normal_path);
        assert(normal.find(" failed destination inaccessible")!=std::string::npos);
        assert(normal.find("destination inaccessible",normal.find("destination inaccessible")+1)==std::string::npos);
        assert(ReadLog(diagnostic_path).empty());assert(DeleteFileW(normal_path.c_str()));
    }
    LogEvent(LogKind::Warning,"ignored","retaining prior file");assert(Take(activity_queue,record));
    assert(FormatLogRecord(record,false).find(" warning retaining prior file")!=std::string::npos);
    // Ordinary diagnostics remain isolated from networking diagnostics.
    InterlockedExchange(&log_level_value,4);settings.log_level=4;QueueDiagnostic("basic observation");
    LogEvent(LogKind::Diagnostic,"playback","state observation");assert(activity_queue.count==0&&diagnostic_queue.count==2);
    auto diagnostics=TakeBatch(diagnostic_queue,records,16);WriteBatch(records,diagnostics,LogLane::Diagnostic);
    assert(ReadLog(normal_path).empty());assert(ReadLog(diagnostic_path).find("basic observation")!=std::string::npos);
    assert(DeleteFileW(diagnostic_path.c_str()));
    InterlockedExchange(&networking_enabled,0);QueueNetworkingDiagnostic("network opt-out");assert(networking_queue.count==0);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=? elapsed_ms=0");assert(networking_queue.count==0);
    InterlockedExchange(&connect_logging_enabled,0);QueueDiagnostic("FLOGGFY_STATUS:control request=pause source=local-client");assert(diagnostic_queue.count==0);
    QueueDiagnostic("FLOGGFY_STATUS:client obs paused=1 buf=0 active=? code=-7 status=503 inst=0 dev=?");assert(diagnostic_queue.count==1);
    InterlockedExchange(&connect_logging_enabled,1);ClearQueues();
    // Omission summaries bypass their saturated queue but are bounded in time.
    diagnostic_queue.next_omission_report=0;diagnostic_queue.omitted.store(7);
    assert(Drain(diagnostic_queue,LogLane::Diagnostic));assert(ReadLog(diagnostic_path).find("records omitted=7")!=std::string::npos);
    diagnostic_queue.omitted.store(9);assert(!Drain(diagnostic_queue,LogLane::Diagnostic));assert(diagnostic_queue.omitted.load()==9);
    assert(ReadLog(normal_path).empty());assert(DeleteFileW(diagnostic_path.c_str()));
    ClearQueues();CloseHandle(signal);signal=nullptr;

    const std::wstring path=root+L"\\SpotifyHistory.ini";
    assert(CreateDefaultIni(path));Settings parsed;
    assert(!ReadRestartOptions(parsed,path)&&!parsed.debug_networking);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugNetworking",nullptr,path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugConnectMessages",L"1",path.c_str()));
    assert(ReadRestartOptions(parsed,path)&&!parsed.debug_networking);
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"Save Location",exe.substr(0,exe.find_last_of(L'\\')).c_str(),path.c_str()));
    InitSettings(nullptr);assert(!NetworkingLoggingEnabled()&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugNetworking",9,path.c_str())==0);
    wchar_t kept[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",kept,16,path.c_str())==2);
    assert(std::wstring(kept)==L"\u97f3\u697d");
    assert(GetFileAttributesW((GetSettings().root+L"\\Floggfy-networking.log").c_str())==INVALID_FILE_ATTRIBUTES);
}
static void SustainedAppends() {
    using namespace history;
    wchar_t temporary[2048];assert(GetTempPathW(2048,temporary));
    const auto path=std::wstring(temporary)+L"floggfy-log-workload-"+std::to_wstring(GetCurrentProcessId())+L".txt";
    DeleteFileW(path.c_str());writes=reads=compactions=0;written_bytes=read_bytes=0;
    const std::string line="α "+std::string(1019,'x')+"\r\n";
    for(unsigned i=0;i<10000;++i)assert(WriteCappedLog(path,line));
    const auto contents=ReadLog(path);assert(contents.size()<=5*1024*1024&&contents.substr(contents.size()-line.size())==line);
    assert(contents.size()%line.size()==0&&contents.substr(0,line.size())==line);
    assert(compactions<=6&&reads==compactions&&read_bytes<30ULL*1024*1024);
    // The old cap-minus-incoming policy compacted after every append once full:
    // 10,000 x 1 KiB lines have 4,880 overflowing appends, versus measured I/O here.
    const unsigned legacy_compactions=10000-5*1024;
    std::printf("MEASURED: 10000 x 1024-byte appends; reads=%u writes=%u compactions=%u read_bytes=%llu written_bytes=%llu; prior policy compactions=%u\n",
        reads,writes,compactions,read_bytes,written_bytes,legacy_compactions);
    assert(compactions*100<legacy_compactions);assert(DeleteFileW(path.c_str()));
}
int wmain() {
    ClientPrivacy();TestCappedLog();QueueAndSettings();SustainedAppends();
    puts("PASS: explicit outcomes, diagnostic isolation/saturation, sanitized renderer fields, opt-outs, bounded batches/omissions, Unicode settings and amortized cap");
    return 0;
}
