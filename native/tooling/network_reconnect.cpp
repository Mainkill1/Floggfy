#define WIN32_LEAN_AND_MEAN
#include "network_reconnect.h"
#include "network_kill_switch.h"
#include "history_settings.h"
#include "settings_file.h"
#include "startup_launcher.h"
#include "process_startup_loader.h"
#include "../telemetry/telemetry_queue.h"
#include <atomic>
#include <cwchar>

namespace {
HMODULE CurrentModule() {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CurrentModule),&module);
    return module;
}
std::wstring ModulePath(HMODULE module) {
    wchar_t path[32768]{};const DWORD count=GetModuleFileNameW(module,path,32768);
    return count&&count<32768?std::wstring(path,count):std::wstring{};
}
bool SaveMode(const std::wstring& ini,bool enabled) {
    std::wstring text;bool unicode=false;
    if(!history::settings_file::Read(ini,text,unicode))return false;
    history::settings_file::Set(text,L"NetworkKillSwitch",enabled?L"1":L"0",L"[Misc]");
    return history::settings_file::Write(ini,text);
}
}
namespace network {
bool RequestReconnect(bool enabled) noexcept {
    static std::atomic<bool> requested{false};
    if(requested.exchange(true))return false;
    struct Retry {std::atomic<bool>& requested;bool started=false;~Retry(){if(!started)requested=false;}} retry{requested};
    try {
        const auto dll=ModulePath(CurrentModule());if(dll.empty())return false;
        wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))return false;
        const std::wstring worker=std::wstring(system)+L"\\rundll32.exe";
        // rundll32 appends W and supplies UTF-16 arguments for this export.
        std::wstring command=L"\""+worker+L"\" \""+dll+L"\",FloggfyReconnect "+(enabled?L"1":L"0");
        // Keep a durable on-state until the external worker completes cleanup.
        if(!SetEnabled(true)||!history::SetNetworkKillSwitch(true)||!history::FlushSettings(5000))return false;
        STARTUPINFOW info{};info.cb=sizeof(info);PROCESS_INFORMATION process{};
        if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&info,&process))return false;
        CloseHandle(process.hThread);CloseHandle(process.hProcess);retry.started=true;return true;
    }catch(...){return false;}
}
}

extern "C" void CALLBACK FloggfyReconnectW(HWND,HINSTANCE,LPWSTR arguments,int) {
    try {
        if(!arguments||(wcscmp(arguments,L"0")&&wcscmp(arguments,L"1")))return;
        const bool enabled=arguments[0]==L'1';
        const auto dll=ModulePath(CurrentModule());const auto slash=dll.find_last_of(L'\\');
        if(slash==dll.npos)return;
        const auto folder=dll.substr(0,slash),ini=folder+L"\\SpotifyHistory.ini",exe=folder+L"\\Spotify.exe";
        std::wstring error;
        if(!startup::StopExisting(exe,error))throw error;
        const auto cleanup=privacy::ClearRoamingTrackingQueues(true);
        if(cleanup.failed||cleanup.skipped)throw std::wstring(L"Telemetry queue cleanup failed (Windows error ")+std::to_wstring(cleanup.last_error)+L"). Network Kill Switch remains enabled.";
        // Any exception or launch failure after publishing the requested mode
        // restores the durable lock before this external worker exits.
        struct Relock {const std::wstring& ini;bool started=false;~Relock(){if(!started)try{SaveMode(ini,true);}catch(...){}}} relock{ini};
        if(!SaveMode(ini,enabled))throw std::wstring(L"Cannot save Network Kill Switch after queue cleanup.");
        PROCESS_INFORMATION process{};bool started=false;
        if(!_wcsicmp(dll.c_str()+slash+1,L"Floggfy.dll"))started=startup::Launch(folder,process,error);
        else {
            std::wstring command=L"\""+exe+L"\"";STARTUPINFOW info{};info.cb=sizeof(info);
            started=startup::CreateWithDll(&CreateProcessW,exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,
                nullptr,folder.c_str(),&info,&process,dll);
        }
        if(!started) {
            throw error.empty()?std::wstring(L"Spotify restart failed. Network Kill Switch remains enabled."):error;
        }
        relock.started=true;
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }catch(const std::wstring& error) {
        MessageBoxW(nullptr,error.c_str(),L"Floggfy reconnect",MB_OK|MB_ICONERROR);
    }catch(...) {
        MessageBoxW(nullptr,L"Spotify reconnect failed. Network Kill Switch remains enabled.",L"Floggfy",MB_OK|MB_ICONERROR);
    }
}
