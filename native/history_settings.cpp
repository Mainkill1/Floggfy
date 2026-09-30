#define WIN32_LEAN_AND_MEAN
#include "history_settings.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "async_log.h"
namespace history {
static SRWLOCK lock=SRWLOCK_INIT;
static Settings settings;
static std::wstring ini,music_path;
static bool IsMusic(std::wstring root) {
    while(!root.empty() && (root.back()==L'\\' || root.back()==L'/')) root.pop_back();
    return !music_path.empty() && (_wcsicmp(root.c_str(),music_path.c_str())==0 ||
        _wcsicmp(root.c_str(),(music_path+L"\\Spotify").c_str())==0);
}
static volatile LONG downloads_enabled=0,ogg_enabled=1,flac_enabled=1,capture_epoch=0,log_enabled=1,debug_enabled=0;
static HANDLE settings_signal=nullptr,persist_done=nullptr;
static volatile LONG persisted_generation=-1,persist_failed=0;
static DWORD WINAPI PersistSettings(LPVOID) {
    for(;;) {
        WaitForSingleObject(settings_signal,INFINITE);
        auto snapshot=GetSettings();
        bool ok=true;
        auto flag=[&](const wchar_t* key,bool value) { if(!WritePrivateProfileStringW(L"To Disk",key,value?L"1":L"0",ini.c_str()))ok=false; };
        flag(L"Downloads",snapshot.downloads);flag(L"Ogg",snapshot.ogg);flag(L"FLAC",snapshot.flac);
        if(!WritePrivateProfileStringW(L"To Disk",L"Save Location",snapshot.save_location.c_str(),ini.c_str()))ok=false;
        InterlockedExchange(&persist_failed,!ok);InterlockedExchange(&persisted_generation,LONG(snapshot.generation));
        SetEvent(persist_done);if(!ok)LogActivity("failed","settings persistence");
    }
}
bool FlushSettings(unsigned timeout_ms) {
    unsigned expected=GetSettings().generation;ULONGLONG end=GetTickCount64()+timeout_ms;
    while(unsigned(InterlockedCompareExchange(&persisted_generation,0,0))!=expected) {
        if(GetTickCount64()>=end)return false;
        WaitForSingleObject(persist_done,10);
    }return !persist_failed;
}
std::wstring WindowsPath(const std::wstring& p) {
    if(p.rfind(L"\\\\?\\",0)==0) return p;
    if(p.rfind(L"\\\\",0)==0) return L"\\\\?\\UNC\\"+p.substr(2);
    if(p.size()>2 && p[1]==L':') return L"\\\\?\\"+p;
    return p;
}
bool EnsureDirectory(const std::wstring& path) {
    if(path.empty()) return false;
    auto p=WindowsPath(path); DWORD a=GetFileAttributesW(p.c_str());
    if(a!=INVALID_FILE_ATTRIBUTES) return (a&FILE_ATTRIBUTE_DIRECTORY)!=0;
    size_t slash=path.find_last_of(L"\\/");
    if(slash!=std::wstring::npos && slash>2 && !EnsureDirectory(path.substr(0,slash))) return false;
    return CreateDirectoryW(p.c_str(),nullptr) || GetLastError()==ERROR_ALREADY_EXISTS;
}
void HistoryLog(const char* message) {
    QueueDiagnostic(message);
    if(std::strstr(message,"failed") || std::strstr(message,"discarded") ||
       std::strstr(message,"rejected") || std::strstr(message,"overflow") ||
       std::strstr(message,"unsupported") || std::strstr(message,"stopped after"))
        LogActivity("failed",message);
}
void InitSettings(HMODULE proxy) {
    wchar_t path[2048]; DWORD n=GetModuleFileNameW(proxy,path,2048);
    if(!n || n>=2048) return;
    wchar_t* slash=wcsrchr(path,L'\\'); if(!slash) return; *slash=0;
    ini=std::wstring(path)+L"\\SpotifyHistory.ini";
    settings.downloads=GetPrivateProfileIntW(L"To Disk",L"Downloads",GetPrivateProfileIntW(L"History",L"Enabled",0,ini.c_str()),ini.c_str())!=0;
    settings.menu=GetPrivateProfileIntW(L"To Disk",L"Menu",1,ini.c_str())!=0;
    settings.ogg=GetPrivateProfileIntW(L"To Disk",L"Ogg",1,ini.c_str())!=0;
    settings.flac=GetPrivateProfileIntW(L"To Disk",L"FLAC",1,ini.c_str())!=0;
    settings.metadata=GetPrivateProfileIntW(L"To Disk",L"Metadata",1,ini.c_str())!=0;
    settings.log=GetPrivateProfileIntW(L"To Disk",L"Log",1,ini.c_str())!=0;
    settings.debug_log=GetPrivateProfileIntW(L"To Disk",L"DebugLog",0,ini.c_str())!=0;
    settings.max_buffered_mib=std::max(8u,std::min(512u,GetPrivateProfileIntW(L"History",L"MaxBufferedMiB",64,ini.c_str())));
    settings.stop_after=GetPrivateProfileIntW(L"History",L"StopAfter",0,ini.c_str());
    wchar_t raw[2048],expanded[4096];
    GetPrivateProfileStringW(L"To Disk",L"Save Location",L"",raw,2048,ini.c_str());
    PWSTR music=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music,0,nullptr,&music))) {
        music_path=music; CoTaskMemFree(music);
    }
    if(!raw[0] && !music_path.empty()) settings.root=music_path+L"\\Spotify";
    DWORD length=ExpandEnvironmentStringsW(raw,expanded,4096);
    if(raw[0] && length && length<=4096) {settings.root=expanded;settings.save_location=raw;}
    settings.music_folder=IsMusic(settings.root);
    downloads_enabled=settings.downloads; ogg_enabled=settings.ogg; flac_enabled=settings.flac;
    log_enabled=settings.log;debug_enabled=settings.debug_log;
    settings_signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);persist_done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(settings_signal&&persist_done){auto thread=CreateThread(nullptr,0,PersistSettings,nullptr,0,nullptr);if(thread)CloseHandle(thread);else{CloseHandle(settings_signal);settings_signal=nullptr;}}
    persisted_generation=LONG(settings.generation);StartLogger();
}
Settings GetSettings() {
    AcquireSRWLockShared(&lock); auto copy=settings; ReleaseSRWLockShared(&lock); return copy;
}
bool LoggingEnabled() { return log_enabled!=0; }
bool DebugLoggingEnabled() { return debug_enabled!=0; }
bool OggEnabled() { return downloads_enabled && ogg_enabled; }
bool FlacEnabled() { return downloads_enabled && flac_enabled; }
unsigned CaptureEpoch() { return unsigned(InterlockedCompareExchange(&capture_epoch,0,0)); }
static bool Set(const wchar_t* key,bool value,bool& field,volatile LONG* fast) {
    AcquireSRWLockExclusive(&lock);
    bool ok=settings_signal!=nullptr;
    (void)key;
    if(ok) {
        if(field!=value) { ++settings.capture_epoch; InterlockedExchange(&capture_epoch,LONG(settings.capture_epoch)); }
        field=value; if(fast) InterlockedExchange(fast,value); ++settings.generation;
    }
    ReleaseSRWLockExclusive(&lock); if(ok)SetEvent(settings_signal); return ok;
}
bool SetDownloads(bool v) { return Set(L"Downloads",v,settings.downloads,&downloads_enabled); }
bool SetOgg(bool v) { return Set(L"Ogg",v,settings.ogg,&ogg_enabled); }
bool SetFlac(bool v) { return Set(L"FLAC",v,settings.flac,&flac_enabled); }
bool SetSaveLocation(const std::wstring& root) {
    if(root.empty() || root.size()>2047 || !(root.size()>2 && (root[1]==L':' || root.rfind(L"\\\\",0)==0))) return false;
    DWORD attributes=GetFileAttributesW(WindowsPath(root).c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    if(attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return false;
    AcquireSRWLockExclusive(&lock);
    bool ok=settings_signal!=nullptr;
    if(ok) { settings.root=root; settings.save_location=root; settings.music_folder=IsMusic(root); ++settings.generation; }
    ReleaseSRWLockExclusive(&lock); if(ok)SetEvent(settings_signal); return ok;
}
static DWORD WINAPI Picker(LPVOID argument) {
    HWND owner=static_cast<HWND>(argument);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(hr)) return 0;
    IFileOpenDialog* dialog=nullptr;
    hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_IFileOpenDialog,reinterpret_cast<void**>(&dialog));
    if(SUCCEEDED(hr)) {
        DWORD flags=0; dialog->GetOptions(&flags); dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"To Disk — Save Location");
        auto current=GetSettings(); IShellItem* initial=nullptr;
        if(SUCCEEDED(SHCreateItemFromParsingName(current.root.c_str(),nullptr,IID_IShellItem,reinterpret_cast<void**>(&initial)))) {
            dialog->SetFolder(initial); initial->Release();
        }
        if(SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item=nullptr; PWSTR path=nullptr;
            if(SUCCEEDED(dialog->GetResult(&item))) {
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                    if(!SetSaveLocation(path)) MessageBoxW(owner,L"The save location could not be saved.",L"To Disk",MB_OK|MB_ICONERROR);
                    CoTaskMemFree(path);
                } item->Release();
            }
        } dialog->Release();
    }
    CoUninitialize(); return 0;
}
void PickSaveLocation(HWND owner) {
    HANDLE thread=CreateThread(nullptr,0,Picker,owner,0,nullptr); if(thread) CloseHandle(thread);
}
}
