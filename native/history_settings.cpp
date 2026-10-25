#define WIN32_LEAN_AND_MEAN
#include "history_settings.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "async_log.h"
#include "srw_guard.h"
#include "settings_file.h"
namespace history {
static SRWLOCK lock=SRWLOCK_INIT;
static Settings settings;
static std::wstring ini,music_path;
static bool IsMusic(std::wstring root) {
    while(!root.empty() && (root.back()==L'\\' || root.back()==L'/')) root.pop_back();
    return !music_path.empty() && (_wcsicmp(root.c_str(),music_path.c_str())==0 ||
        _wcsicmp(root.c_str(),(music_path+L"\\Spotify").c_str())==0);
}
static volatile LONG downloads_enabled=1,ogg_enabled=1,flac_enabled=1,aac_enabled=0,capture_epoch=0,log_enabled=1,debug_enabled=0;
static HANDLE settings_signal=nullptr,persist_done=nullptr;
// A failed attempt is observable without acknowledging bytes never committed.
static volatile LONG persisted_generation=-1,failed_generation=-1,persist_failed=0;
static constexpr char default_ini[]=
    "[To Disk]\r\n"
    "; Settings also appear in the To Disk menu.\r\n"
    "Downloads=1\r\n"
    "; Set Menu=0 before starting Spotify to disable native menu integration.\r\n"
    "Menu=1\r\n"
    "; Workaround when disabled/blocked Microsoft online checks (Windows NCSI)\r\n"
    "; make Windows report \"No Internet\" despite a working connection.\r\n"
    "; Reports an active adapter with a usable IP as Internet-connected to Spotify,\r\n"
    "; helping it avoid false-offline restrictions on playback/quality selection.\r\n"
    "; Spotify-only: does not change Windows network status or re-enable the checks.\r\n"
    "; Does not test connection speed or force highest quality; choose it in Spotify.\r\n"
    "; 0=off (default), 1=on. Fully quit and restart Spotify after changing.\r\n"
    "NetworkPatch=0\r\n"
    "; Empty uses the Windows Music folder plus \\Spotify.\r\n"
    "Save Location=\r\n"
    "; Prefix new filenames with the track number when known.\r\n"
    "TrackNumberInFilename=0\r\n"
    "; At track start, rename verified old numbered files without rewriting audio.\r\n"
    "; TrackNumberInFilename=1 disables conversion.\r\n"
    "ConvertNumberedFilenames=0\r\n"
    "Ogg=1\r\n"
    "; Captures native FLAC when Spotify supplies lossless audio.\r\n"
    "Flac=1\r\n"
    "; AAC is usually a low-quality source; opt in explicitly.\r\n"
    "AAC=0\r\n"
    "; Enables metadata enrichment using the local Spotify client.\r\n"
    "Metadata=1\r\n"
    "; Automatically obtains lyrics; Metadata=0 disables all enrichment.\r\n"
    "Lyrics=1\r\n"
    "; Allows metadata-only upgrades to existing saved files.\r\n"
    "UpdateExistingMetadata=1\r\n"
    "; Activity log in Save Location, capped at 5 MiB.\r\n"
    "Log=1\r\n"
    "DebugLog=0\r\n"
    "; With DebugLog=1, log playback-device changes and local client control requests.\r\n"
    "; Remote command senders may be unknown. Device names and IDs are not logged.\r\n"
    "DebugConnect=1\r\n"
    "; Experimental read-only decoded Connect requests, including pause/resume.\r\n"
    "; Requires Log=1 and DebugLog=1; restart. Default off; changes no commands.\r\n"
    "; Logs action/message ID and anonymous sender labels, not raw messages.\r\n"
    "DebugConnectMessages=0\r\n"
    "\r\n"
    "[History]\r\n"
    "; Maximum compressed audio held in RAM for one track.\r\n"
    "MaxBufferedMiB=500\r\n";
static bool CreateDefaultIni(const std::wstring& path) {
    std::wstring text;for(const char* p=default_ini;*p;++p)text+=wchar_t(*p);
    return settings_file::Write(path,text,true);
}
// Missing/blank options and conflicting enabled flags need an atomic rewrite.
// Reading does not change capture epochs or acknowledge persistence.
static bool ReadFilenameOptions(Settings& target,const std::wstring& path) {
    bool needs_persist=false;
    auto option=[&](const wchar_t* key) {
        wchar_t value[2]{};
        if(!GetPrivateProfileStringW(L"To Disk",key,L"",value,2,path.c_str())) {
            needs_persist=true;
            return false;
        }
        return GetPrivateProfileIntW(L"To Disk",key,0,path.c_str())!=0;
    };
    target.track_number_in_filename=option(L"TrackNumberInFilename");
    target.convert_numbered_filenames=option(L"ConvertNumberedFilenames");
    if(target.track_number_in_filename && target.convert_numbered_filenames) {
        target.convert_numbered_filenames=false;
        needs_persist=true;
    }
    return needs_persist;
}
// Old installations without the key also default off. Queue an atomic write
// to make the opt-in discoverable without replacing unrelated INI preferences.
static bool ReadNetworkPatchOption(Settings& target,const std::wstring& path) {
    wchar_t value[2]{};
    const bool present=GetPrivateProfileStringW(L"To Disk",L"NetworkPatch",L"",value,2,path.c_str())!=0;
    target.network_patch=GetPrivateProfileIntW(L"To Disk",L"NetworkPatch",0,path.c_str())!=0;
    return !present;
}
static bool ReadDebugConnectOption(Settings& target,const std::wstring& path) {
    wchar_t value[2]{};
    const bool present=GetPrivateProfileStringW(L"To Disk",L"DebugConnect",L"",value,2,path.c_str())!=0;
    target.debug_connect=GetPrivateProfileIntW(L"To Disk",L"DebugConnect",1,path.c_str())!=0;
    return !present;
}
static bool ReadDecodedConnectOption(Settings& target,const std::wstring& path) {
    wchar_t value[2]{};
    const bool present=GetPrivateProfileStringW(L"To Disk",L"DebugConnectMessages",L"",value,2,path.c_str())!=0;
    target.debug_connect_messages=GetPrivateProfileIntW(L"To Disk",L"DebugConnectMessages",0,path.c_str())!=0;
    return !present;
}
static bool ReachedGeneration(volatile LONG& progress,unsigned expected) {
    return static_cast<LONG>(unsigned(InterlockedCompareExchange(&progress,0,0))-expected)>=0;
}
static DWORD WINAPI PersistSettings(LPVOID) {
    for(;;) {
        WaitForSingleObject(settings_signal,INFINITE);
        unsigned generation=0;bool ok=false,snapshot_captured=false;
        try {
            const auto snapshot=GetSettings();generation=snapshot.generation;snapshot_captured=true;
            // A notification can remain signaled while the previous write already
            // includes its change. Do not open another staging file after Flush
            // has acknowledged this generation. Failed generations still retry.
            if(ReachedGeneration(persisted_generation,generation))continue;
            std::wstring text;bool unicode=false;
            ok=settings_file::Read(ini,text,unicode);
            if(ok) {
                auto flag=[&](const wchar_t* key,bool value){settings_file::Set(text,key,value?L"1":L"0");};
                flag(L"Downloads",snapshot.downloads);flag(L"Ogg",snapshot.ogg);flag(L"FLAC",snapshot.flac);flag(L"AAC",snapshot.aac);flag(L"Lyrics",snapshot.lyrics);
                flag(L"UpdateExistingMetadata",snapshot.update_existing_metadata);
                flag(L"NetworkPatch",snapshot.network_patch);
                flag(L"DebugConnect",snapshot.debug_connect);
                flag(L"DebugConnectMessages",snapshot.debug_connect_messages);
                flag(L"TrackNumberInFilename",snapshot.track_number_in_filename);
                flag(L"ConvertNumberedFilenames",snapshot.convert_numbered_filenames && !snapshot.track_number_in_filename);
                settings_file::Set(text,L"Save Location",snapshot.save_location);
                ok=settings_file::Write(ini,text);
            }
        } catch(...) {
            ok=false;
            if(!snapshot_captured){SharedSrw guard(lock);generation=settings.generation;}
        }
        // Publish status before its generation acknowledgement boundary.
        InterlockedExchange(&persist_failed,!ok);
        if(ok)InterlockedExchange(&persisted_generation,LONG(generation));
        else InterlockedExchange(&failed_generation,LONG(generation));
        SetEvent(persist_done);
        if(!ok)try{LogActivity("failed","settings persistence");}catch(...) {/* A logging allocation must not kill persistence retries. */}
    }
}
bool FlushSettings(unsigned timeout_ms) {
    const auto expected=GetSettings().generation;const auto end=GetTickCount64()+timeout_ms;
    for(;;) {
        if(ReachedGeneration(persisted_generation,expected))return true;
        if(ReachedGeneration(failed_generation,expected))
            return ReachedGeneration(persisted_generation,expected); // A concurrent retry may just have succeeded.
        if(!persist_done||GetTickCount64()>=end)return false;
        WaitForSingleObject(persist_done,10);
    }
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
    DWORD attributes=GetFileAttributesW(ini.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES &&
       (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND) &&
       !CreateDefaultIni(ini))
        QueueDiagnostic("failed to create default SpotifyHistory.ini; using built-in settings");
    if(!settings_file::Unicode(ini))QueueDiagnostic("failed to migrate settings; original file retained");
    settings.downloads=GetPrivateProfileIntW(L"To Disk",L"Downloads",GetPrivateProfileIntW(L"History",L"Enabled",1,ini.c_str()),ini.c_str())!=0;
    settings.menu=GetPrivateProfileIntW(L"To Disk",L"Menu",1,ini.c_str())!=0;
    settings.ogg=GetPrivateProfileIntW(L"To Disk",L"Ogg",1,ini.c_str())!=0;
    settings.flac=GetPrivateProfileIntW(L"To Disk",L"FLAC",1,ini.c_str())!=0;
    const bool filename_options_pending=ReadFilenameOptions(settings,ini);
    const bool network_option_pending=ReadNetworkPatchOption(settings,ini);
    const bool connect_option_pending=ReadDebugConnectOption(settings,ini);
    const bool decoded_option_pending=ReadDecodedConnectOption(settings,ini);
    const bool startup_options_pending=filename_options_pending || network_option_pending || connect_option_pending || decoded_option_pending;
    settings.aac=GetPrivateProfileIntW(L"To Disk",L"AAC",0,ini.c_str())!=0;
    settings.metadata=GetPrivateProfileIntW(L"To Disk",L"Metadata",1,ini.c_str())!=0;
    settings.lyrics=GetPrivateProfileIntW(L"To Disk",L"Lyrics",1,ini.c_str())!=0;
    settings.update_existing_metadata=GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",1,ini.c_str())!=0;
    settings.log=GetPrivateProfileIntW(L"To Disk",L"Log",1,ini.c_str())!=0;
    settings.debug_log=GetPrivateProfileIntW(L"To Disk",L"DebugLog",0,ini.c_str())!=0;
    settings.max_buffered_mib=std::max(8u,std::min(512u,GetPrivateProfileIntW(L"History",L"MaxBufferedMiB",500,ini.c_str())));
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
    downloads_enabled=settings.downloads; ogg_enabled=settings.ogg; flac_enabled=settings.flac; aac_enabled=settings.aac;
    log_enabled=settings.log;debug_enabled=settings.debug_log;
    settings_signal=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    persist_done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE thread=settings_signal&&persist_done ? CreateThread(nullptr,0,PersistSettings,nullptr,0,nullptr) : nullptr;
    if(thread) CloseHandle(thread);
    else {
        if(settings_signal) CloseHandle(settings_signal);
        if(persist_done) CloseHandle(persist_done);
        settings_signal=nullptr;persist_done=nullptr;
    }
    // Missing options/conflict normalization are pending until Write succeeds.
    InterlockedExchange(&persisted_generation,startup_options_pending?-1:LONG(settings.generation));
    if(startup_options_pending && settings_signal)SetEvent(settings_signal);
    StartLogger();
}
Settings GetSettings() {
    SharedSrw guard(lock);return settings;
}
bool LoggingEnabled() { return log_enabled!=0; }
bool DebugLoggingEnabled() { return debug_enabled!=0; }
bool OggEnabled() { return downloads_enabled && ogg_enabled; }
bool FlacEnabled() { return downloads_enabled && flac_enabled; }
bool AACEnabled() { return downloads_enabled && aac_enabled; }
unsigned CaptureEpoch() { return unsigned(InterlockedCompareExchange(&capture_epoch,0,0)); }
static bool Set(const wchar_t* key,bool value,bool& field,volatile LONG* fast,bool affects_capture=true) {
    bool ok=false;
    {
    ExclusiveSrw guard(lock);
    ok=settings_signal!=nullptr;
    (void)key;
    if(ok) {
        if(affects_capture && field!=value) { ++settings.capture_epoch; InterlockedExchange(&capture_epoch,LONG(settings.capture_epoch)); }
        field=value; if(fast) InterlockedExchange(fast,value); ++settings.generation;
    }
    }
    if(ok)SetEvent(settings_signal);
    return ok;
}
bool SetDownloads(bool v) { return Set(L"Downloads",v,settings.downloads,&downloads_enabled); }
bool SetOgg(bool v) { return Set(L"Ogg",v,settings.ogg,&ogg_enabled); }
bool SetFlac(bool v) { return Set(L"FLAC",v,settings.flac,&flac_enabled); }
bool SetLyrics(bool v) { return Set(L"Lyrics",v,settings.lyrics,nullptr,false); }
bool SetUpdateExistingMetadata(bool v) { return Set(L"UpdateExistingMetadata",v,settings.update_existing_metadata,nullptr,false); }
bool SetSaveLocation(const std::wstring& root) {
    if(root.empty() || root.size()>2047 || !(root.size()>2 && (root[1]==L':' || root.rfind(L"\\\\",0)==0))) return false;
    DWORD attributes=GetFileAttributesW(WindowsPath(root).c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    if(attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return false;
    auto next_root=root,next_location=root;const bool music=IsMusic(root);
    bool ok=false;
    {ExclusiveSrw guard(lock);ok=settings_signal!=nullptr;
     if(ok) {settings.root.swap(next_root);settings.save_location.swap(next_location);settings.music_folder=music;++settings.generation;}}
    if(ok)SetEvent(settings_signal);
    return ok;
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
