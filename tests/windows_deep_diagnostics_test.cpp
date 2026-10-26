// Synthetic headers and deterministic clocks only; no Spotify/network operations.
#include "../native/history_settings.cpp"
#include "../native/async_log.cpp"
#include "../native/hook_diagnostics.h"
#include "../native/launcher_architecture.h"
#include "../native/startup_launcher.h"
#include <cassert>
#include <vector>

static void Activities() {
    using namespace history::diagnostics;
    ActivityTracker tracker;std::vector<ActivityObservation> events;
    auto poll=[&](uint64_t now){tracker.Poll(now,[&](const auto& e){events.push_back(e);});};
    auto short_call=tracker.Begin(Phase::MenuFormat,7,100);tracker.Finish(short_call,109);
    poll(110);assert(events.empty());
    auto slow=tracker.Begin(Phase::MenuFormat,7,200);tracker.Finish(slow,210);
    poll(211);assert(events.size()==1&&!events.back().pending&&events.back().elapsed_ms==10);
    poll(212);assert(events.size()==1);
    auto stuck=tracker.Begin(Phase::MenuCreate,8,300);
    poll(549);assert(events.size()==1);
    poll(550);assert(events.size()==2&&events.back().pending&&events.back().elapsed_ms==250);
    poll(551);assert(events.size()==2);
    poll(5550);assert(events.size()==3);
    tracker.Finish(stuck,5600);poll(5601);assert(events.size()==4&&!events.back().pending);
    std::array<ActivityToken,ActivityTracker::capacity> tokens;
    for(auto& token:tokens){token=tracker.Begin(Phase::MenuSubmenu,9,6000);assert(token.Valid());}
    assert(!tracker.Begin(Phase::MenuSubmenu,9,6001).Valid()&&tracker.TakeOmitted()==1);
    for(const auto& token:tokens)tracker.Finish(token,6002);
    auto reused=tracker.Begin(Phase::MenuExecute,10,7000);
    tracker.Finish(short_call,99999); // A stale token cannot complete its reused slot.
    poll(7250);assert(events.back().pending&&events.back().phase==Phase::MenuExecute);
    tracker.Finish(reused,6999);poll(7251); // Backwards clocks never become giant durations.
}
static void GatingAndSettings() {
    using namespace history;
    assert(!Settings{}.debug_hook_diagnostics);
    InterlockedExchange(&log_enabled,1);InterlockedExchange(&debug_enabled,1);
    InterlockedExchange(&networking_enabled,1);
    SetLastError(0x12345678);
    {HookDiagnosticScope scope(diagnostics::Phase::MenuFormat);}
    assert(GetLastError()==0x12345678&&!HookDiagnosticsEnabled());
    InterlockedExchange(&hook_diagnostics_enabled,1);
    SetLastError(0x12345678);
    {HookDiagnosticScope scope(diagnostics::Phase::MenuFormat);SetLastError(0x87654321);}
    assert(GetLastError()==0x87654321&&HookDiagnosticsEnabled());
    InterlockedExchange(&networking_enabled,0);assert(!HookDiagnosticsEnabled());
    InterlockedExchange(&networking_enabled,1);InterlockedExchange(&debug_enabled,0);assert(!HookDiagnosticsEnabled());
    InterlockedExchange(&debug_enabled,1);InterlockedExchange(&log_enabled,0);assert(!HookDiagnosticsEnabled());
    InterlockedExchange(&log_enabled,1);InterlockedExchange(&hook_diagnostics_enabled,0);
    wchar_t exe[32768];assert(GetModuleFileNameW(nullptr,exe,32768));
    const std::wstring full=exe,path=full.substr(0,full.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    assert(CreateDefaultIni(path));Settings parsed;
    assert(!ReadRestartOptions(parsed,path)&&!parsed.debug_hook_diagnostics);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugHookDiagnostics",L"1",path.c_str()));
    assert(!ReadRestartOptions(parsed,path)&&parsed.debug_hook_diagnostics);
    assert(WritePrivateProfileStringW(L"To Disk",L"DebugHookDiagnostics",nullptr,path.c_str()));
    assert(ReadRestartOptions(parsed,path)&&!parsed.debug_hook_diagnostics);
    assert(WritePrivateProfileStringW(L"Other",L"Keep",L"\u97f3\u697d",path.c_str()));
    InitSettings(nullptr);assert(!HookDiagnosticsEnabled()&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"To Disk",L"DebugHookDiagnostics",9,path.c_str())==0);
    wchar_t text[16];assert(GetPrivateProfileStringW(L"Other",L"Keep",L"",text,16,path.c_str())==2);
    assert(std::wstring(text)==L"\u97f3\u697d");
}
static void Progress() {
    history::diagnostics::ProgressWatch watch;
    assert(watch.Observe(0,1000)==history::diagnostics::ProgressState::None);
    assert(watch.Observe(100,1099)==history::diagnostics::ProgressState::None);
    assert(watch.Observe(100,1100)==history::diagnostics::ProgressState::Stalled);
    assert(watch.Observe(100,1101)==history::diagnostics::ProgressState::None);
    assert(watch.Observe(100,6100)==history::diagnostics::ProgressState::Stalled);
    assert(watch.Observe(6101,6102)==history::diagnostics::ProgressState::Recovered);
    assert(watch.Observe(0,7000)==history::diagnostics::ProgressState::None);
}
static void Architectures() {
    using namespace startup;
    wchar_t exe[32768];assert(GetModuleFileNameW(nullptr,exe,32768));
    const std::wstring full=exe,root=full.substr(0,full.find_last_of(L'\\'));
    auto image=[](WORD machine,WORD magic){
        std::vector<unsigned char> bytes(512);IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=64;
        std::memcpy(bytes.data(),&dos,sizeof(dos));DWORD signature=IMAGE_NT_SIGNATURE;
        std::memcpy(bytes.data()+64,&signature,4);IMAGE_FILE_HEADER file{};file.Machine=machine;
        file.NumberOfSections=1;
        file.SizeOfOptionalHeader=magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC?sizeof(IMAGE_OPTIONAL_HEADER64):sizeof(IMAGE_OPTIONAL_HEADER32);
        std::memcpy(bytes.data()+68,&file,sizeof(file));
        std::memcpy(bytes.data()+68+sizeof(file),&magic,2);return bytes;
    };
    auto write=[&](const std::wstring& path,const std::vector<unsigned char>& bytes){
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);assert(file!=INVALID_HANDLE_VALUE);
        DWORD size=0;assert(WriteFile(file,bytes.data(),DWORD(bytes.size()),&size,nullptr)&&size==bytes.size());CloseHandle(file);
    };
    auto x64=image(IMAGE_FILE_MACHINE_AMD64,IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    const auto spotify=root+L"\\Spotify.exe",dll=root+L"\\Floggfy.dll";
    write(spotify,image(IMAGE_FILE_MACHINE_I386,IMAGE_NT_OPTIONAL_HDR32_MAGIC));write(dll,x64);
    PROCESS_INFORMATION process{};std::wstring error;
    assert(!Launch(root,process,error)&&error.find(L"32-bit")!=std::wstring::npos&&!process.hProcess);
    write(spotify,x64);write(dll,image(IMAGE_FILE_MACHINE_ARM64,IMAGE_NT_OPTIONAL_HDR64_MAGIC));
    assert(!Launch(root,process,error)&&error.find(L"ARM64")!=std::wstring::npos&&!process.hProcess);
    write(dll,std::vector<unsigned char>(5));
    assert(!Launch(root,process,error)&&error.find(L"inspect")!=std::wstring::npos);
    write(dll,x64);assert(InspectArchitecture(dll).architecture==Architecture::X64);
    auto truncated=x64;truncated.resize(100);write(dll,truncated);
    assert(!Launch(root,process,error)&&error.find(L"inspect")!=std::wstring::npos&&!process.hProcess);
    auto undersized=x64;WORD two=2;
    std::memcpy(undersized.data()+68+offsetof(IMAGE_FILE_HEADER,SizeOfOptionalHeader),&two,sizeof(two));write(dll,undersized);
    assert(!Launch(root,process,error)&&error.find(L"inspect")!=std::wstring::npos&&!process.hProcess);
    x64[60]=0xff;x64[61]=0xff;x64[62]=0xff;x64[63]=0x7f;write(dll,x64);
    assert(InspectArchitecture(dll).architecture==Architecture::Invalid);
    DeleteFileW(spotify.c_str());DeleteFileW(dll.c_str());
}
int wmain(){Activities();Progress();Architectures();GatingAndSettings();puts("PASS: bounded activity/stall diagnostics, opt-in/LastError safety, Unicode settings and prelaunch architecture rejection");return 0;}
