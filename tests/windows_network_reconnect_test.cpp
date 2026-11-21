// Drive the production reconnect worker with isolated INI data and process/queue
// boundaries replaced. No installed Spotify processes or queues are touched.
#include "../native/tooling/network_reconnect.h"
#include "../native/tooling/startup_launcher.h"
#include "../native/tooling/process_startup_loader.h"
#include "../native/tooling/settings_file.h"
#include "../native/tooling/history_settings.h"
#include "../native/telemetry/telemetry_queue.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<int> steps;
static bool stop_ok=true,start_ok=true,queue_ok=true;
static unsigned messages=0;
static std::wstring ini;
static int WINAPI Report(HWND,LPCWSTR,LPCWSTR,UINT){++messages;return IDOK;}
#define MessageBoxW Report
#include "../native/tooling/network_reconnect.cpp"
#undef MessageBoxW
namespace network {bool SetEnabled(bool){return true;}}
namespace history {
bool SetNetworkKillSwitch(bool){return true;}
bool FlushSettings(unsigned){return true;}
}
namespace privacy {
QueueCleanupResult ClearRoamingTrackingQueues(bool enabled) noexcept {
    assert(enabled&&steps==std::vector<int>{1});steps.push_back(2);
    QueueCleanupResult result;if(!queue_ok)result.failed=1;return result;
}
}
namespace startup {
bool StopExisting(const std::wstring&,std::wstring&) {steps.push_back(1);return stop_ok;}
bool Launch(const std::wstring&,PROCESS_INFORMATION&,std::wstring&){assert(false);return false;}
BOOL CreateWithDll(CreateProcessFunction,LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,
 BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION process,const std::wstring&) noexcept {
    assert((steps==std::vector<int>{1,2}));steps.push_back(3);
    if(!start_ok)return FALSE;
    process->hThread=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    process->hProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);return TRUE;
}
}
int main() {
    wchar_t module[32768]{};assert(GetModuleFileNameW(nullptr,module,32768));
    std::wstring folder=module;folder.resize(folder.find_last_of(L'\\'));
    ini=folder+L"\\SpotifyHistory.ini";
    const std::wstring baseline=L"; keep preferences\n[Telemetry]\nBlockTelemetry=0\nBlockSentry=0\nBlockQueuedTelemetry=0\n[Misc]\nNetworkKillSwitch=1\n";
    auto mode=[] {
        history::settings_file::Snapshot file(ini);assert(file.readable);
        assert(file.text.find(L"BlockTelemetry=0")!=file.text.npos);
        assert(file.text.find(L"BlockSentry=0")!=file.text.npos);
        assert(file.text.find(L"BlockQueuedTelemetry=0")!=file.text.npos);
        return std::wstring(history::settings_file::Find(file.text,L"NetworkKillSwitch",L"[Misc]").value);
    };
    auto reset=[&] {
        DeleteFileW(ini.c_str());assert(history::settings_file::Write(ini,baseline,true));
        steps.clear();messages=0;stop_ok=start_ok=queue_ok=true;
    };
    wchar_t off[]=L"0",on[]=L"1";
    reset();FloggfyReconnectW(nullptr,GetModuleHandleW(nullptr),off,0);
    assert((steps==std::vector<int>{1,2,3})&&mode()==L"0"&&!messages);
    reset();FloggfyReconnectW(nullptr,GetModuleHandleW(nullptr),on,0);
    assert((steps==std::vector<int>{1,2,3})&&mode()==L"1"&&!messages);
    reset();stop_ok=false;FloggfyReconnectW(nullptr,GetModuleHandleW(nullptr),off,0);
    assert(steps==std::vector<int>{1}&&mode()==L"1"&&messages==1);
    reset();queue_ok=false;FloggfyReconnectW(nullptr,GetModuleHandleW(nullptr),off,0);
    assert((steps==std::vector<int>{1,2})&&mode()==L"1"&&messages==1);
    reset();start_ok=false;FloggfyReconnectW(nullptr,GetModuleHandleW(nullptr),off,0);
    assert((steps==std::vector<int>{1,2,3})&&mode()==L"1"&&messages==1);
    assert(DeleteFileW(ini.c_str()));
    puts("PASS: stop -> clear -> restart, preferences preserved, failures retain lock");
}
