// Isolated files/settings only. Never loads Spotify or sends network requests.
#include "../native/history_settings.cpp"
#include "../native/async_log.cpp"
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
static void QueueAndSettings() {
    using namespace history;
    assert(!Settings{}.debug_networking);
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,root=exe.substr(0,exe.find_last_of(L'\\'));
    const auto log_path=root+L"\\Floggfy-networking.log";
    settings.root=root;settings.log=true;settings.debug_log=true;
    signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(signal);
    InterlockedExchange(&log_enabled,1);InterlockedExchange(&debug_enabled,1);
    QueueNetworkingDiagnostic("test disabled");assert(network_queue.count==0);
    InterlockedExchange(&networking_enabled,1);
    InterlockedExchange(&debug_enabled,0);QueueNetworkingDiagnostic("test no debug");assert(network_queue.count==0);
    InterlockedExchange(&debug_enabled,1);InterlockedExchange(&log_enabled,0);
    QueueNetworkingDiagnostic("test no log");assert(network_queue.count==0);
    InterlockedExchange(&log_enabled,1);
    QueueDiagnostic("FLOGGFY_STATUS:control request=pause source=local-client");
    assert(network_queue.count==1&&activity_queue.count==0);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=? elapsed_ms=0");
    assert(network_queue.count==2);
    QueueDiagnostic("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=send code=PRIVATE_TOKEN elapsed_ms=0");
    assert(network_queue.count==2&&activity_queue.count==0);
    for(unsigned i=0;i<128;++i)QueueNetworkingDiagnostic("bounded test");
    assert(network_queue.count==128&&network_omitted.load()==2);
    LogActivity("finished","normal activity still fits");assert(activity_queue.count==1);
    AcquireSRWLockExclusive(&network_queue.lock);
    QueueNetworkingDiagnostic("contention never waits");
    ReleaseSRWLockExclusive(&network_queue.lock);assert(network_omitted.load()==3);
    Record record{};assert(Take(network_queue,record));assert(record.sequence==1&&record.observed_ms>0);
    assert(std::string(record.detail).find("request=pause")!=std::string::npos);
    const auto formatted=FormatLogRecord(record,true);
    assert(formatted.find("pid=")!=std::string::npos&&formatted.find("seq=1")!=std::string::npos);
    assert(formatted.find("mono_ms=")!=std::string::npos&&formatted.find("direction=local")!=std::string::npos);
    WriteRecord(record,true);
    HANDLE saved=CreateFileW(log_path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(saved!=INVALID_HANDLE_VALUE);
    char persisted[1200]{};DWORD read=0;assert(ReadFile(saved,persisted,sizeof(persisted)-1,&read,nullptr));CloseHandle(saved);
    assert(std::string(persisted,read)==formatted&&DeleteFileW(log_path.c_str()));
    InterlockedExchange(&networking_enabled,0);WriteRecord(record,true);
    assert(GetFileAttributesW(log_path.c_str())==INVALID_FILE_ATTRIBUTES);
    while(Take(network_queue,record)){}
    while(Take(activity_queue,record)){}
    CloseHandle(signal);signal=nullptr;network_omitted.store(0);

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
int wmain() {
    ClientPrivacy();TestCappedLog();QueueAndSettings();
    puts("PASS: networking master opt-out, strict field privacy, separate bounded queues, timestamps, omission accounting, Unicode and whole-line cap");
    return 0;
}
