// Exercise the real renderer gateway and every logger path, with isolated files.
#include "../native/history_settings.cpp"
#include "../native/async_log.cpp"
#include <cassert>
#include <vector>

namespace {
constexpr const char* marker="EXAMPLE_PRIVATE_SENTINEL";
std::wstring test_root;
void EmptyQueues() {
    assert(history::activity_queue.count==0&&history::network_queue.count==0);
}
std::string ReadText(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE){assert(GetLastError()==ERROR_FILE_NOT_FOUND);return {};}
    LARGE_INTEGER size{};assert(GetFileSizeEx(file,&size)&&size.QuadPart<65536);
    std::string text(size_t(size.QuadPart),'\0');DWORD got=0;
    assert(ReadFile(file,text.data(),DWORD(text.size()),&got,nullptr)&&got==text.size());CloseHandle(file);
    return text;
}
void FlushAndCheck() {
    history::Record record;
    while(history::Take(history::activity_queue,record)) {
        assert(std::string(record.detail).find(marker)==std::string::npos);
        assert(std::string(record.detail).find("FLOGGFY_")==std::string::npos);
        history::WriteRecord(record,false);
    }
    while(history::Take(history::network_queue,record)) {
        assert(std::string(record.detail).find(marker)==std::string::npos);
        assert(std::string(record.detail).find("FLOGGFY_")==std::string::npos);
        history::WriteRecord(record,true);
    }
    for(const auto name:{L"Floggfy.log",L"Floggfy-networking.log"}) {
        const auto text=ReadText(test_root+L"\\"+name);
        assert(text.find(marker)==std::string::npos&&text.find("FLOGGFY_")==std::string::npos);
    }
    EmptyQueues();
}
void Mode(bool log,bool debug,bool network) {
    using namespace history;
    InterlockedExchange(&log_enabled,log);InterlockedExchange(&debug_enabled,debug);
    InterlockedExchange(&networking_enabled,network);
    settings.log=log;settings.debug_log=debug;settings.debug_networking=network;
}
void RejectedEverywhere() {
    using namespace history;
    const std::vector<std::string> messages={
        "FLOGGFY_STATUS:connect failed EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:control rejected EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:unknown overflow EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:connect discarded EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:connect unsupported EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:connect stopped after EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:cached player found failed EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_STATUS:connect state=connected target=local device_changed=1 controller=unknown failed EXAMPLE_PRIVATE_SENTINEL",
        "FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=rejected code=EXAMPLE_PRIVATE_SENTINEL elapsed_ms=1",
        "FLOGGFY_STATUS:client obs paused=1 buf=0 active=? code=-7 status=503 inst=0 dev=?\nEXAMPLE_PRIVATE_SENTINEL",
        std::string("FLOGGFY_STATUS:")+std::string(1000,'x')+" failed EXAMPLE_PRIVATE_SENTINEL"
    };
    for(unsigned bits=0;bits<8;++bits) {
        Mode((bits&1)!=0,(bits&2)!=0,(bits&4)!=0);
        for(const auto& message:messages) {
            HistoryLog(message.c_str());QueueDiagnostic(message.c_str());
            LogActivity("failed",message);QueueNetworkingDiagnostic(message.c_str());
            const std::wstring wide(message.begin(),message.end());
            assert(QueueRendererDiagnostic(wide));EmptyQueues();
        }
        const wchar_t embedded[]=L"FLOGGFY_STATUS:cached player found\0failed EXAMPLE_PRIVATE_SENTINEL";
        assert(QueueRendererDiagnostic({embedded,sizeof(embedded)/sizeof(*embedded)-1}));EmptyQueues();
        const char narrow[]= "FLOGGFY_STATUS:cached player found\0failed EXAMPLE_PRIVATE_SENTINEL";
        LogActivity("failed",std::string(narrow,sizeof(narrow)-1));EmptyQueues();
        assert(QueueRendererDiagnostic(L"FLOGGFY_STATUS:connect failed \u97f3\u697d"));EmptyQueues();
        assert(!QueueRendererDiagnostic(L"ordinary Spotify console message"));EmptyQueues();
    }
}
void ValidRouting() {
    using namespace history;
    const char* status="FLOGGFY_STATUS:control request=pause source=local-client";
    for(bool network:{false,true}) {
        Mode(true,true,network);
        HistoryLog(status);
        assert(activity_queue.count==size_t(!network)&&network_queue.count==size_t(network));
        FlushAndCheck();
        assert(QueueRendererDiagnostic(L"FLOGGFY_STATUS:connect state=connected target=local device_changed=1 controller=unknown"));
        assert(activity_queue.count==size_t(!network)&&network_queue.count==size_t(network));FlushAndCheck();
        HistoryLog("FLOGGFY_NETWORK_V1:request op=1 kind=lyrics phase=rejected code=503 elapsed_ms=1");
        assert(activity_queue.count==0&&network_queue.count==size_t(network));FlushAndCheck();
        HistoryLog("FLOGGFY_STATUS:metadata collector started");
        HistoryLog("FLOGGFY_STATUS:cached player found");
        assert(activity_queue.count==2&&network_queue.count==0);FlushAndCheck();
    }
    Mode(true,false,true);HistoryLog("FLOGGFY_STATUS:connect failed EXAMPLE_PRIVATE_SENTINEL");EmptyQueues();
    HistoryLog("native capture failed; original file retained");
    assert(activity_queue.count==1&&network_queue.count==0);FlushAndCheck(); // Core failures still log without DebugLog.
    const auto activity=ReadText(test_root+L"\\Floggfy.log");
    const auto networking=ReadText(test_root+L"\\Floggfy-networking.log");
    assert(activity.find("source=client-control")!=std::string::npos);
    assert(activity.find("native capture failed")!=std::string::npos);
    assert(networking.find("source=client-control")!=std::string::npos&&networking.find("phase=rejected code=503")!=std::string::npos);
}
}
int wmain() {
    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring path=executable;test_root=path.substr(0,path.find_last_of(L'\\'));
    history::settings.root=test_root;history::signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(history::signal);
    RejectedEverywhere();ValidRouting();
    CloseHandle(history::signal);history::signal=nullptr;
    puts("PASS: renderer privacy across all logger paths/modes, failure-keyword bypass rejection, bounded Unicode/NUL handling, valid routing and actual file contents");
    return 0;
}
