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
    history::activity_queue.omitted.store(0);history::diagnostic_queue.omitted.store(0);
}
static void QueueAndSettings() {
    using namespace history;
    assert(!Settings{}.debug_networking);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,root=exe.substr(0,exe.find_last_of(L'\\'));
    const auto diagnostic_path=root+L"\\Floggfy-diagnostics.log",normal_path=root+L"\\Floggfy.log";
    DeleteFileW(normal_path.c_str());DeleteFileW(diagnostic_path.c_str());
    settings.root=root;settings.log=true;settings.debug_log=true;
    signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(signal);writes=0;
    InterlockedExchange(&log_enabled,1);InterlockedExchange(&debug_enabled,1);
    QueueNetworkingDiagnostic("test disabled");assert(diagnostic_queue.count==0);
    InterlockedExchange(&networking_enabled,1);
    InterlockedExchange(&debug_enabled,0);QueueNetworkingDiagnostic("test no debug");assert(diagnostic_queue.count==0);
    InterlockedExchange(&debug_enabled,1);InterlockedExchange(&log_enabled,0);
    QueueNetworkingDiagnostic("test no log");assert(diagnostic_queue.count==0);
    // Every reserved renderer message remains claimed even when disabled.
    assert(QueueRendererDiagnostic(L"FLOGGFY_STATUS:PRIVATE_FIELD=SECRET"));
    assert(activity_queue.count==0&&diagnostic_queue.count==0);
    InterlockedExchange(&log_enabled,1);
    QueueDiagnostic("FLOGGFY_STATUS:control request=pause source=local-client");
    assert(diagnostic_queue.count==1&&activity_queue.count==0);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=? elapsed_ms=0");
    assert(diagnostic_queue.count==2);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=PRIVATE_TOKEN elapsed_ms=0");
    assert(diagnostic_queue.count==2&&activity_queue.count==0);
    for(unsigned i=0;i<128;++i)QueueDiagnostic("bounded basic diagnostic");
    assert(diagnostic_queue.count==128&&diagnostic_queue.omitted.load()==2);
    LogEvent(LogKind::Activity,"finished","normal activity still fits");assert(activity_queue.count==1);
    AcquireSRWLockExclusive(&diagnostic_queue.lock);QueueDiagnostic("contention never waits");ReleaseSRWLockExclusive(&diagnostic_queue.lock);
    assert(diagnostic_queue.omitted.load()==3);
    Record record{};assert(Take(diagnostic_queue,record));assert(record.sequence==1&&record.observed_ms>0);
    assert(std::string(record.detail).find("request=pause")!=std::string::npos);
    const auto formatted=FormatLogRecord(record,true);
    assert(formatted.find("pid=")!=std::string::npos&&formatted.find("seq=1")!=std::string::npos);
    assert(formatted.find("mono_ms=")!=std::string::npos&&formatted.find("direction=local")!=std::string::npos);
    assert(writes==0);WriteBatch(&record,1,true);
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
    writes=0;WriteBatch(records,count,false);assert(writes==1);
    auto normal=ReadLog(normal_path);for(const char* keyword:{"failed","discarded","rejected","overflow","unsupported","stopped after"})
        assert(normal.find(std::string("finished saved ")+keyword+".ogg")!=std::string::npos);
    const auto same=normal.find("finished saved same title.ogg");assert(same!=std::string::npos&&normal.find("finished saved same title.ogg",same+1)!=std::string::npos);
    assert(normal.find(" failed ")==std::string::npos);assert(DeleteFileW(normal_path.c_str()));
    for(int debug=0;debug<2;++debug) {
        InterlockedExchange(&debug_enabled,debug);settings.debug_log=debug!=0;
        LogEvent(LogKind::Failure,"ignored","destination inaccessible");
        assert(activity_queue.count==1&&diagnostic_queue.count==0);assert(Take(activity_queue,record));
        WriteBatch(&record,1,false);normal=ReadLog(normal_path);
        assert(normal.find(" failed destination inaccessible")!=std::string::npos);
        assert(normal.find("destination inaccessible",normal.find("destination inaccessible")+1)==std::string::npos);
        assert(ReadLog(diagnostic_path).empty());assert(DeleteFileW(normal_path.c_str()));
    }
    LogEvent(LogKind::Warning,"ignored","retaining prior file");assert(Take(activity_queue,record));
    assert(FormatLogRecord(record,false).find(" warning retaining prior file")!=std::string::npos);
    // Both ordinary and networking diagnostics use only the diagnostic queue/file.
    InterlockedExchange(&debug_enabled,1);settings.debug_log=true;QueueDiagnostic("basic observation");
    LogEvent(LogKind::Diagnostic,"playback","state observation");assert(activity_queue.count==0&&diagnostic_queue.count==2);
    auto diagnostics=TakeBatch(diagnostic_queue,records,16);WriteBatch(records,diagnostics,true);
    assert(ReadLog(normal_path).empty());assert(ReadLog(diagnostic_path).find("basic observation")!=std::string::npos);
    assert(DeleteFileW(diagnostic_path.c_str()));
    InterlockedExchange(&networking_enabled,0);QueueNetworkingDiagnostic("network opt-out");assert(diagnostic_queue.count==0);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=? elapsed_ms=0");assert(diagnostic_queue.count==0);
    InterlockedExchange(&connect_logging_enabled,0);QueueDiagnostic("FLOGGFY_STATUS:control request=pause source=local-client");assert(diagnostic_queue.count==0);
    QueueDiagnostic("FLOGGFY_STATUS:client obs paused=1 buf=0 active=? code=-7 status=503 inst=0 dev=?");assert(diagnostic_queue.count==1);
    InterlockedExchange(&connect_logging_enabled,1);ClearQueues();
    // Omission summaries bypass their saturated queue but are bounded in time.
    diagnostic_queue.next_omission_report=0;diagnostic_queue.omitted.store(7);
    assert(Drain(diagnostic_queue,true));assert(ReadLog(diagnostic_path).find("records omitted=7")!=std::string::npos);
    diagnostic_queue.omitted.store(9);assert(!Drain(diagnostic_queue,true));assert(diagnostic_queue.omitted.load()==9);
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
    assert(GetFileAttributesW((GetSettings().root+L"\\Floggfy-diagnostics.log").c_str())==INVALID_FILE_ATTRIBUTES);
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
