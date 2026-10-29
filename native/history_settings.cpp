#define WIN32_LEAN_AND_MEAN
#include "history_settings.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
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
static volatile LONG downloads_enabled=1,ogg_enabled=1,flac_enabled=1,aac_enabled=0,capture_epoch=0,log_enabled=1,debug_enabled=0,networking_enabled=0,connect_logging_enabled=1,hook_diagnostics_enabled=0;
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
    "Aac=0\r\n"
    "; Enables metadata enrichment using the local Spotify client.\r\n"
    "Metadata=1\r\n"
    "; Automatically obtains lyrics; Metadata=0 disables all enrichment.\r\n"
    "Lyrics=1\r\n"
    "; Allows metadata-only upgrades to existing saved files.\r\n"
    "UpdateExistingMetadata=1\r\n"
    "\r\n"
    "; Normal activity, warnings and failures: Floggfy.log in Save Location (5 MiB).\r\n"
    "; Log=0 also disables ALL debug output, hooks and timing instrumentation below.\r\n"
    "Log=1\r\n"
    "\r\n"
    "; Debug options require Log=1. Fully restart Spotify after INI edits.\r\n"
    "; Optional details go only to Floggfy-diagnostics.log (5 MiB), never Floggfy.log.\r\n"
    "DebugLog=0\r\n"
    "; Requires DebugLog=1: cached playback target and local actions.\r\n"
    "; Set 0 to opt out, including when DebugNetworking=1.\r\n"
    "; Use exactly 1 to enable. Device names/IDs are omitted; senders may be unknown.\r\n"
    "DebugConnect=1\r\n"
    "; Deep read-only networking diagnostics; default off. Fully restart after edits.\r\n"
    "; Requires Log=1 and DebugLog=1. Use exactly 1 to enable; 0/invalid values disable.\r\n"
    "; Disabled means no decoded-message hook or enrichment network observations.\r\n"
    "; 1 adds networking observations to Floggfy-diagnostics.log.\r\n"
    "; Summarizes known Connect/control and enrichment I/O with timing for correlation.\r\n"
    "; No raw messages, media, credentials, URLs, device names or device IDs are logged.\r\n"
    "; Unobserved traffic and command execution remain unknown; no commands are changed.\r\n"
    "DebugNetworking=0\r\n"
    "; Extra callback/stall diagnostics. Requires DebugNetworking=1, Log=1, DebugLog=1.\r\n"
    "; Default off; exactly 1 enables callback profiling and slow/pending activities.\r\n"
    "; Basic DebugLog does no per-callback profiling; no dumps or raw payloads.\r\n"
    "DebugHookDiagnostics=0\r\n"
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
// INI-only preferences are read once. Persistence inserts missing defaults,
// but never writes a stale startup snapshot over values edited before restart.
struct RestartFlag {
    const wchar_t* key;
    bool Settings::* member;
    bool default_value;
};
static constexpr std::array<RestartFlag,4> restart_flags{{
    {L"NetworkPatch",&Settings::network_patch,false},
    {L"DebugConnect",&Settings::debug_connect,true},
    {L"DebugNetworking",&Settings::debug_networking,false},
    {L"DebugHookDiagnostics",&Settings::debug_hook_diagnostics,false}
}};
static bool ReadRestartOptionsText(Settings& target,std::wstring_view text) {
    bool missing=false;
    for(const auto& flag:restart_flags) {
        const auto value=settings_file::Find(text,flag.key);
        missing=missing||!value.present;
        target.*(flag.member)=!value.present?flag.default_value:
            !value.duplicate&&value.value==L"1";
    }
    return missing;
}
static bool ReadRestartOptions(Settings& target,const std::wstring& path) {
    std::wstring text;bool unicode=false;
    if(!settings_file::Read(path,text,unicode)) {
        for(const auto& flag:restart_flags)target.*(flag.member)=false;
        return false;
    }
    return ReadRestartOptionsText(target,text);
}
static void InsertMissingRestartDefaults(std::wstring& text) {
    for(const auto& flag:restart_flags)
        if(!settings_file::Find(text,flag.key).present)
            settings_file::Set(text,flag.key,flag.default_value?L"1":L"0");
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
                InsertMissingRestartDefaults(text);
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
        if(!ok)try{LogEvent(LogKind::Failure,"failed","settings persistence; edits remain queued for retry");}catch(...) {/* A logging allocation must not kill persistence retries. */}
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
void InitSettings(HMODULE proxy) {
    wchar_t path[2048]; DWORD n=GetModuleFileNameW(proxy,path,2048);
    if(!n || n>=2048) return;
    wchar_t* slash=wcsrchr(path,L'\\'); if(!slash) return; *slash=0;
    ini=std::wstring(path)+L"\\SpotifyHistory.ini";
    DWORD attributes=GetFileAttributesW(ini.c_str());
    const bool default_ini_failed=attributes==INVALID_FILE_ATTRIBUTES &&
       (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND) &&
       !CreateDefaultIni(ini);
    const bool migration_failed=!default_ini_failed&&!settings_file::Unicode(ini);
    settings.downloads=GetPrivateProfileIntW(L"To Disk",L"Downloads",GetPrivateProfileIntW(L"History",L"Enabled",1,ini.c_str()),ini.c_str())!=0;
    settings.menu=GetPrivateProfileIntW(L"To Disk",L"Menu",1,ini.c_str())!=0;
    settings.ogg=GetPrivateProfileIntW(L"To Disk",L"Ogg",1,ini.c_str())!=0;
    settings.flac=GetPrivateProfileIntW(L"To Disk",L"FLAC",1,ini.c_str())!=0;
    const bool filename_options_pending=ReadFilenameOptions(settings,ini);
    const bool restart_options_pending=ReadRestartOptions(settings,ini);
    const bool startup_options_pending=filename_options_pending||restart_options_pending;
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
    log_enabled=settings.log;debug_enabled=settings.debug_log;networking_enabled=settings.debug_networking;connect_logging_enabled=settings.debug_connect;hook_diagnostics_enabled=settings.debug_hook_diagnostics;
    StartLogger();
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
    if(default_ini_failed)LogEvent(LogKind::Warning,"warning","default SpotifyHistory.ini could not be created; using built-in settings");
    if(migration_failed)LogEvent(LogKind::Warning,"warning","settings Unicode migration could not be completed; original file retained");
    if(!thread)LogEvent(LogKind::Failure,"failed","settings persistence worker unavailable; changes cannot be saved");
}
Settings GetSettings() {
    SharedSrw guard(lock);return settings;
}
bool LoggingEnabled() { return log_enabled!=0; }
bool DebugLoggingEnabled() { return DiagnosticsEnabled(log_enabled!=0,debug_enabled!=0); }
bool ConnectLoggingEnabled() { return connect_logging_enabled!=0 && DebugLoggingEnabled(); }
bool NetworkingLoggingEnabled() { return networking_enabled!=0 && DebugLoggingEnabled(); }
bool HookDiagnosticsEnabled() { return hook_diagnostics_enabled!=0 && NetworkingLoggingEnabled(); }
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
