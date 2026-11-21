#define WIN32_LEAN_AND_MEAN
#include "history_settings.h"
#include "windows_known_folder.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <optional>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include "async_log.h"
#include "srw_guard.h"
#include "settings_file.h"
#include "../telemetry/telemetry_options.h"
#include "network_kill_switch_settings.h"
#include "../media/capture_speed.h"
namespace history {
static SRWLOCK lock=SRWLOCK_INIT;
static Settings settings;
static std::wstring ini,music_path;
static bool IsMusic(std::wstring root) {
    while(!root.empty() && (root.back()==L'\\' || root.back()==L'/')) root.pop_back();
    return !music_path.empty() && (_wcsicmp(root.c_str(),music_path.c_str())==0 ||
        _wcsicmp(root.c_str(),(music_path+L"\\Spotify").c_str())==0);
}
static volatile LONG downloads_enabled=1,ogg_enabled=1,flac_enabled=1,aac_enabled=0,capture_epoch=0,capture_speed_value=-1,capture_acceleration_available=0,log_level_value=1,networking_enabled=0,connect_logging_enabled=0,hook_diagnostics_enabled=0;
static HANDLE settings_signal=nullptr,persist_done=nullptr;
// A failed attempt is observable without acknowledging bytes never committed.
static volatile LONG persisted_generation=-1,failed_generation=-1,persist_failed=0;
#include "../../build/default_ini.h"
static bool CreateDefaultIni(const std::wstring& path) {
    std::wstring text;for(const char* p=default_ini;*p;++p)text+=wchar_t(*p);
    return settings_file::Write(path,text,true);
}
struct SectionOption {const wchar_t* key;const wchar_t* section;const wchar_t* legacy_key=nullptr;};
static constexpr std::array<SectionOption,13> section_options{{
    {L"MarkedWhenDownloaded",L"[Misc]"},
    {L"SaveUnmatched",L"[Matcher]"},
    {L"WindowsPlaybackFallback",L"[Fallbacks]",L"Windows Playback Fallback"},
    {L"LogLevel",L"[Debug]"},
    {L"DebugConnect",L"[Debug]"},
    {L"DebugNetworking",L"[Debug]"},
    {L"DebugHookDiagnostics",L"[Debug]"},
    {L"DumpStreams",L"[Debug]"},
    {L"MetadataCacheReadMiB",L"[History]"},
    {L"NetworkPatch",L"[Misc]"},
    {L"UpdateExistingMetadata",L"[Misc]"},
    {L"TrackNumberInFilename",L"[Misc]"},
    {L"ConvertNumberedFilenames",L"[Misc]"}
}};
static const SectionOption* SectionFor(const wchar_t* key) {
    for(const auto& option:section_options)if(settings_file::EqualKey(key,option.key))return &option;
    return nullptr;
}
static settings_file::IniValue CategorizedValue(std::wstring_view text,const wchar_t* key) {
    const auto* option=SectionFor(key);if(!option)return settings_file::Find(text,key);
    auto value=settings_file::Find(text,key,option->section);
    if(!value.present)value=settings_file::Find(text,key);
    if(!value.present && option->legacy_key)value=settings_file::Find(text,option->legacy_key);
    return value;
}
// Keep Win32 integer semantics for existing boolean preferences. Presence in
// the canonical section wins even when its value is blank.
static UINT ReadCategorizedInt(const settings_file::Snapshot& file,const wchar_t* key,UINT initial,bool* missing=nullptr) {
    const auto* option=SectionFor(key);
    const auto section=option&&settings_file::Find(file.text,key,option->section).present?
        std::wstring(option->section).substr(1,std::wcslen(option->section)-2):L"To Disk";
    if(missing) {
        wchar_t value[2]{};
        *missing=!GetPrivateProfileStringW(section.c_str(),key,L"",value,2,file.path.c_str());
    }
    return GetPrivateProfileIntW(section.c_str(),key,initial,file.path.c_str());
}
// Missing/blank options and conflicting enabled flags need an atomic rewrite.
// Reading does not change capture epochs or acknowledge persistence.
static bool ReadFilenameOptions(Settings& target,const settings_file::Snapshot& file) {
    bool needs_persist=false;
    auto option=[&](const wchar_t* key) {
        bool missing=false;
        const bool enabled=ReadCategorizedInt(file,key,0,&missing)!=0;
        needs_persist=needs_persist||missing;
        return enabled;
    };
    target.track_number_in_filename=option(L"TrackNumberInFilename");
    target.convert_numbered_filenames=option(L"ConvertNumberedFilenames");
    if(target.track_number_in_filename && target.convert_numbered_filenames) {
        target.convert_numbered_filenames=false;
        needs_persist=true;
    }
    return needs_persist;
}
static bool ReadCaptureSpeed(Settings& target,const settings_file::Snapshot& file) {
    return file.ReadChoice(settings_file::Find(file.text,L"CaptureSpeed"),target.capture_speed,-1,{-1,0,1,2,4,8,10});
}
static bool CategoriesNeedMigration(std::wstring_view text) {
    for(const auto& option:section_options)
        if(!settings_file::Find(text,option.key,option.section).present||settings_file::Find(text,option.key).present||
           (option.legacy_key && settings_file::Find(text,option.legacy_key).present))return true;
    return settings_file::Find(text,L"CaptureSpeedLimit").present;
}
static void MigrateCategories(std::wstring& text) {
    for(const auto& option:section_options) {
        const auto value=CategorizedValue(text,option.key);
        if(!settings_file::Find(text,option.key,option.section).present&&value.present) {
            // Duplicate opt-ins remain disabled after migration; scalar options
            // use their defaults rather than accepting one conflicting value.
            auto migrated=std::wstring(value.raw_value);
            if(value.duplicate)migrated=settings_file::EqualKey(option.key,L"LogLevel")?L"1":
                settings_file::EqualKey(option.key,L"MetadataCacheReadMiB")?L"256":L"0";
            settings_file::Set(text,option.key,migrated,option.section);
        }
        settings_file::Erase(text,option.key);
        if(option.legacy_key)settings_file::Erase(text,option.legacy_key);
    }
    settings_file::Erase(text,L"CaptureSpeedLimit");
    // These exact old generated comments describe the removed setting.
    for(const wchar_t* comment:{L"; 0 disables acceleration and removes the speed menu; 1 enables it.",L"; -1 No shown in menu, 0 is disabled, 1 enables it."}) {
        const auto pos=text.find(comment);if(pos!=text.npos){const auto end=text.find(L'\n',pos);text.erase(pos,end==text.npos?text.size()-pos:end+1-pos);}
    }
}
struct FallbackOption {const wchar_t* key;bool Settings::*member;};
static constexpr FallbackOption fallback_options[]={
    {L"ReadSpotifyDiskCache",&Settings::read_spotify_disk_cache},
    {L"FetchMissingMetadata",&Settings::fetch_missing_metadata},
    {L"FetchMissingArtwork",&Settings::fetch_missing_artwork},
    {L"FetchMissingLyrics",&Settings::fetch_missing_lyrics},
};
static bool ReadEnrichmentFallbacks(Settings& target,const settings_file::Snapshot& file) {
    if(!file.readable)return false;
    bool pending=false;
    for(const auto& option:fallback_options) {
        const auto value=settings_file::Find(file.text,option.key,L"[Fallbacks]");
        pending|=file.ReadChoice(value,target.*(option.member),1,{0,1});
    }
    return pending;
}
static bool ReadWindowsPlaybackFallback(Settings& target,const settings_file::Snapshot& file) {
    return file.ReadChoice(CategorizedValue(file.text,L"WindowsPlaybackFallback"),target.windows_playback_fallback,0,{0,1});
}
static bool ReadBlockUpdates(Settings& target,const settings_file::Snapshot& file) {
    return file.ReadChoice(settings_file::Find(file.text,L"BlockUpdates",L"[Misc]"),target.block_updates,0,{0,1});
}
static bool ReadNetworkKillSwitch(Settings& target,const settings_file::Snapshot& file) {
    return network::ReadConfiguredMode(file,target.network_kill_switch);
}
// INI-only preferences are read once. Persistence inserts missing defaults,
// but never writes a stale startup snapshot over values edited before restart.
struct RestartFlag {
    const wchar_t* key;
    bool Settings::* member;
    bool default_value;
};
static constexpr std::array<RestartFlag,6> restart_flags{{
    {L"SaveUnmatched",&Settings::save_unmatched,true},
    {L"NetworkPatch",&Settings::network_patch,false},
    {L"DumpStreams",&Settings::dump_streams,false},
    {L"DebugConnect",&Settings::debug_connect,false},
    {L"DebugNetworking",&Settings::debug_networking,false},
    {L"DebugHookDiagnostics",&Settings::debug_hook_diagnostics,false}
}};
struct BlockingOptionValue {
    std::optional<bool> current,previous,disk;
    bool Enabled(bool fallback) const {return current.value_or(previous.value_or(disk.value_or(fallback)));}
};
struct BlockingIni {
    std::array<BlockingOptionValue,privacy::kTelemetryOptions.size()> values{};
    bool has_previous_section=false;

    explicit BlockingIni(std::wstring_view text) {
        using Field=std::optional<bool> BlockingOptionValue::*;
        Field field=nullptr;
        settings_file::VisitEntries(text,[&](auto section) {
            if(settings_file::EqualKey(section,L"[Telemetry]"))field=&BlockingOptionValue::current;
            else if(settings_file::EqualKey(section,L"[Blocking]")) {
                field=&BlockingOptionValue::previous;has_previous_section=true;
            }else if(settings_file::EqualKey(section,L"[To Disk]"))field=&BlockingOptionValue::disk;
            else field=nullptr;
            return field!=nullptr;
        },[&](auto key,auto raw) {
            for(size_t i=0;i<privacy::kTelemetryOptions.size();++i) {
                const auto& flag=privacy::kTelemetryOptions[i];
                if(field==&BlockingOptionValue::disk&&flag.IsCategory())continue;
                if(!settings_file::EqualKey(key,flag.key))continue;
                auto& value=values[i].*field;
                // Duplicate and malformed entries in the winning section disable the option.
                value=!value.has_value()&&raw==L"1";
                break;
            }
        });
    }
};
static bool ReadBlockingOptionsText(Settings& target,std::wstring_view text) {
    const BlockingIni ini(text);
    bool migration=false;
    for(size_t i=0;i<privacy::kTelemetryOptions.size();++i) {
        const auto& flag=privacy::kTelemetryOptions[i];const auto& value=ini.values[i];
        target.*(flag.member)=value.Enabled(flag.default_value);
        migration|=!value.current.has_value()||value.previous.has_value()||value.disk.has_value();
    }
    return migration;
}
static void MigrateBlockingOptions(std::wstring& text) {
    const BlockingIni ini(text);
    for(size_t i=0;i<privacy::kTelemetryOptions.size();++i) {
        const auto& flag=privacy::kTelemetryOptions[i];const auto& value=ini.values[i];
        if(!value.current.has_value())
            settings_file::Set(text,flag.key,value.Enabled(flag.default_value)?L"1":L"0",L"[Telemetry]");
        if(value.previous.has_value())settings_file::Erase(text,flag.key,L"[Blocking]");
        if(value.disk.has_value())settings_file::Erase(text,flag.key);
    }
    if(!ini.has_previous_section)return;
    // Remove empty legacy headers while retaining comments and unknown keys.
    for(size_t position=0;position<text.size();) {
        const auto end=text.find(L'\n',position);
        const auto next=end==text.npos?text.size():end+1;
        if(settings_file::EqualKey(settings_file::TrimView(std::wstring_view(text).substr(position,(end==text.npos?text.size():end)-position)),L"[Blocking]")) {
            bool empty=true;
            for(size_t scan=next;scan<text.size();) {
                const auto finish=text.find(L'\n',scan);
                const auto after=finish==text.npos?text.size():finish+1;
                const auto line=settings_file::TrimView(std::wstring_view(text).substr(scan,(finish==text.npos?text.size():finish)-scan));
                if(!line.empty()&&line.front()==L'['&&line.back()==L']')break;
                if(!line.empty()&&line.front()!=L';'){empty=false;break;}
                scan=after;
            }
            if(empty){text.erase(position,next-position);continue;}
        }
        position=next;
    }
}
static bool ParseUnsignedSetting(const settings_file::IniValue& value,unsigned minimum,unsigned maximum,unsigned& parsed) {
    parsed=0;
    if(!value.present||value.duplicate||value.value.empty())return false;
    for(const auto digit:value.value) {
        if(digit<L'0'||digit>L'9'||parsed>maximum/10)return false;
        parsed=parsed*10+unsigned(digit-L'0');
        if(parsed>maximum)return false;
    }
    return parsed>=minimum;
}
static bool ReadRestartOptionsText(Settings& target,std::wstring_view text) {
    bool missing=false;
    for(const auto& flag:restart_flags) {
        const auto value=CategorizedValue(text,flag.key);
        missing=missing||!value.present;
        target.*(flag.member)=!value.present?flag.default_value:
            !value.duplicate&&value.value==L"1";
    }
    const auto budget=CategorizedValue(text,L"MetadataCacheReadMiB");
    unsigned parsed=0;
    target.metadata_cache_read_mib=ParseUnsignedSetting(budget,1,4096,parsed)?parsed:256;
    return ReadBlockingOptionsText(target,text)||missing||!budget.present||CategoriesNeedMigration(text);
}
static bool ReadRestartOptions(Settings& target,const settings_file::Snapshot& file) {
    if(!file.readable) {
        for(const auto& flag:restart_flags)target.*(flag.member)=false;
        for(const auto& flag:privacy::kTelemetryOptions)target.*(flag.member)=false;
        return false;
    }
    return ReadRestartOptionsText(target,file.text);
}
struct MatcherOption {const wchar_t* key;unsigned Settings::*member;unsigned initial,minimum,maximum;};
static constexpr std::array<MatcherOption,3> matcher_options{{
    {L"MaxMatchQueue",&Settings::matcher_max_queue,5,1,100},
    {L"TimeOffsetToleranceMs",&Settings::matcher_time_offset_ms,10000,0,120000},
    {L"DurationToleranceMs",&Settings::matcher_duration_ms,25,0,2000}
}};
static bool ReadMatcherOptionsText(Settings& target,std::wstring_view text) {
    const auto legacy=settings_file::Find(text,L"TimingToleranceSeconds",L"[Matcher]");
    unsigned legacy_seconds=0;
    const bool valid_legacy=ParseUnsignedSetting(legacy,0,120,legacy_seconds);
    bool needs_persist=legacy.present;
    for(const auto& option:matcher_options) {
        const auto value=settings_file::Find(text,option.key,L"[Matcher]");
        unsigned parsed=0;
        const bool valid=ParseUnsignedSetting(value,option.minimum,option.maximum,parsed);
        const bool migrate=!value.present&&valid_legacy&&option.member==&Settings::matcher_time_offset_ms;
        target.*(option.member)=valid?parsed:migrate?legacy_seconds*1000:option.initial;
        needs_persist=needs_persist||!valid;
    }
    return needs_persist;
}
static bool ReadMatcherOptions(Settings& target,const settings_file::Snapshot& file) {
    return file.readable&&ReadMatcherOptionsText(target,file.text);
}
static void InsertMissingRestartDefaults(std::wstring& text) {
    MigrateCategories(text);MigrateBlockingOptions(text);
    if(!settings_file::Find(text,L"MarkedWhenDownloaded",L"[Misc]").present)
        settings_file::Set(text,L"MarkedWhenDownloaded",L"1",L"[Misc]");
    if(!settings_file::Find(text,L"MetadataCacheReadMiB",L"[History]").present)
        settings_file::Set(text,L"MetadataCacheReadMiB",L"256",L"[History]");
    const auto log=settings_file::Find(text,L"LogLevel",L"[Debug]");
    if(!log.present||log.duplicate||(log.raw_value!=L"0"&&log.raw_value!=L"1"&&log.raw_value!=L"2"&&log.raw_value!=L"4"))
        settings_file::Set(text,L"LogLevel",L"1",L"[Debug]");
    for(const auto& flag:restart_flags) {
        const auto* option=SectionFor(flag.key);const auto* section=option?option->section:L"[To Disk]";
        if(!settings_file::Find(text,flag.key,section).present)
            settings_file::Set(text,flag.key,flag.default_value?L"1":L"0",section);
    }
    Settings migrated;ReadMatcherOptionsText(migrated,text);
    for(const auto& option:matcher_options)
        if(!settings_file::Find(text,option.key,L"[Matcher]").present)
            settings_file::Set(text,option.key,std::to_wstring(migrated.*(option.member)),L"[Matcher]");
    settings_file::Erase(text,L"TimingToleranceSeconds",L"[Matcher]");
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
                auto flag=[&](const wchar_t* key,bool value){
                    const auto* option=SectionFor(key);
                    settings_file::Set(text,key,value?L"1":L"0",option?option->section:L"[To Disk]");
                };
                flag(L"Downloads",snapshot.downloads);flag(L"Ogg",snapshot.ogg);flag(L"FLAC",snapshot.flac);flag(L"AAC",snapshot.aac);flag(L"Podcasts",snapshot.podcasts);flag(L"Lyrics",snapshot.lyrics);
                InsertMissingRestartDefaults(text);
                for(const auto& option:fallback_options)
                    settings_file::Set(text,option.key,snapshot.*(option.member)?L"1":L"0",L"[Fallbacks]");
                settings_file::Set(text,L"WindowsPlaybackFallback",snapshot.windows_playback_fallback?L"1":L"0",L"[Fallbacks]");
                settings_file::Set(text,L"CaptureSpeed",std::to_wstring(snapshot.capture_speed));
                flag(L"UpdateExistingMetadata",snapshot.update_existing_metadata);
                flag(L"AutoSkipDownloaded",snapshot.auto_skip_downloaded);
                settings_file::Set(text,L"NeverPause",std::to_wstring(snapshot.never_pause));
                settings_file::Set(text,L"NetworkKillSwitch",std::to_wstring(snapshot.network_kill_switch),L"[Misc]");
                settings_file::Set(text,L"BlockUpdates",snapshot.block_updates?L"1":L"0",L"[Misc]");
                flag(L"MarkedWhenDownloaded",snapshot.marked_when_downloaded);
                settings_file::Erase(text,L"Log");settings_file::Erase(text,L"DebugLog");
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
    const auto native_path=WindowsPath(path);
    DWORD attributes=GetFileAttributesW(native_path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        const auto slash=path.find_last_of(L"\\/");
        if(slash!=std::wstring::npos && slash>2 && !EnsureDirectory(path.substr(0,slash))) return false;
        if(CreateDirectoryW(native_path.c_str(),nullptr)) return true;
        if(GetLastError()!=ERROR_ALREADY_EXISTS) return false;
        attributes=GetFileAttributesW(native_path.c_str());
    }
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
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
    const settings_file::Snapshot file(ini);
    const bool migration_failed=!default_ini_failed&&(!file.readable||(!file.unicode&&!settings_file::Write(ini,file.text)));
    settings.downloads=GetPrivateProfileIntW(L"To Disk",L"Downloads",GetPrivateProfileIntW(L"History",L"Enabled",1,ini.c_str()),ini.c_str())!=0;
    settings.menu=GetPrivateProfileIntW(L"To Disk",L"Menu",1,ini.c_str())!=0;
    settings.ogg=GetPrivateProfileIntW(L"To Disk",L"Ogg",1,ini.c_str())!=0;
    settings.flac=GetPrivateProfileIntW(L"To Disk",L"FLAC",1,ini.c_str())!=0;
    bool startup_options_pending=false;
    settings.marked_when_downloaded=ReadCategorizedInt(file,L"MarkedWhenDownloaded",1,&startup_options_pending)!=0;
    startup_options_pending|=ReadFilenameOptions(settings,file);
    startup_options_pending|=ReadCaptureSpeed(settings,file);
    startup_options_pending|=ReadEnrichmentFallbacks(settings,file);
    startup_options_pending|=ReadWindowsPlaybackFallback(settings,file);
    startup_options_pending|=file.ReadChoice(settings_file::Find(file.text,L"AutoSkipDownloaded"),settings.auto_skip_downloaded,0,{0,1});
    startup_options_pending|=file.ReadChoice(settings_file::Find(file.text,L"NeverPause"),settings.never_pause,-1,{-1,0,1});
    startup_options_pending|=ReadNetworkKillSwitch(settings,file);
    startup_options_pending|=ReadBlockUpdates(settings,file);
    startup_options_pending|=file.ReadChoice(CategorizedValue(file.text,L"LogLevel"),settings.log_level,1,{0,1,2,4});
    startup_options_pending|=file.readable&&(settings_file::Find(file.text,L"Log").present||settings_file::Find(file.text,L"DebugLog").present);
    startup_options_pending|=ReadRestartOptions(settings,file);
    startup_options_pending|=ReadMatcherOptions(settings,file);
    settings.aac=GetPrivateProfileIntW(L"To Disk",L"AAC",0,ini.c_str())!=0;
    settings.podcasts=GetPrivateProfileIntW(L"To Disk",L"Podcasts",1,ini.c_str())!=0;
    settings.metadata=GetPrivateProfileIntW(L"To Disk",L"Metadata",1,ini.c_str())!=0;
    settings.lyrics=GetPrivateProfileIntW(L"To Disk",L"Lyrics",1,ini.c_str())!=0;
    settings.update_existing_metadata=ReadCategorizedInt(file,L"UpdateExistingMetadata",1)!=0;
    settings.max_buffered_mib=std::max(8u,std::min(kMaximumBufferedMiB,GetPrivateProfileIntW(L"History",L"MaxBufferedMiB",kDefaultBufferedMiB,ini.c_str())));
    wchar_t raw[2048],expanded[4096];
    GetPrivateProfileStringW(L"To Disk",L"Save Location",L"",raw,2048,ini.c_str());
    ReadKnownFolder(FOLDERID_Music,music_path);
    if(!raw[0] && !music_path.empty()) settings.root=music_path+L"\\Spotify";
    DWORD length=ExpandEnvironmentStringsW(raw,expanded,4096);
    if(raw[0] && length && length<=4096) {settings.root=expanded;settings.save_location=raw;}
    settings.music_folder=IsMusic(settings.root);
    downloads_enabled=settings.downloads; ogg_enabled=settings.ogg; flac_enabled=settings.flac; aac_enabled=settings.aac;
    capture_speed_value=LONG(settings.capture_speed);
    log_level_value=LONG(settings.log_level);networking_enabled=settings.debug_networking;connect_logging_enabled=settings.debug_connect;hook_diagnostics_enabled=settings.debug_hook_diagnostics;
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
bool LoggingEnabled() { return LogLevel()!=0; }
unsigned LogLevel() {return unsigned(InterlockedCompareExchange(&log_level_value,0,0));}
bool DebugLoggingEnabled() { return LogLevel()>=4; }
bool ConnectLoggingEnabled() { return connect_logging_enabled!=0 && DebugLoggingEnabled(); }
bool NetworkingLoggingEnabled() { return networking_enabled!=0 && DebugLoggingEnabled(); }
bool HookDiagnosticsEnabled() { return hook_diagnostics_enabled!=0 && DebugLoggingEnabled(); }
bool OggEnabled() { return downloads_enabled && ogg_enabled; }
bool FlacEnabled() { return downloads_enabled && flac_enabled; }
bool AACEnabled() { return downloads_enabled && aac_enabled; }
bool DownloadsEnabled() { return downloads_enabled!=0; }
unsigned CaptureEpoch() { return unsigned(InterlockedCompareExchange(&capture_epoch,0,0)); }
unsigned CaptureSpeed() { return unsigned(std::max<LONG>(1,InterlockedCompareExchange(&capture_speed_value,0,0))); }
bool CaptureAccelerationAvailable() {return InterlockedCompareExchange(&capture_acceleration_available,0,0)!=0;}
void SetCaptureAccelerationAvailable(bool available) {InterlockedExchange(&capture_acceleration_available,available?1:0);}
bool CaptureSpeedAvailable(unsigned speed) {
    return InterlockedCompareExchange(&capture_speed_value,0,0)>=0&&capture_speed::Supported(speed)&&
        (speed==1||CaptureAccelerationAvailable());
}
static unsigned enrichment_enable_revision=0;
unsigned EnrichmentEnableRevision() {
    SharedSrw guard(lock);return enrichment_enable_revision;
}
template<class Update>
static bool ChangeSettings(Update update) {
    {
        ExclusiveSrw guard(lock);
        if(!settings_signal||!update())return false;
        ++settings.generation;
    }
    SetEvent(settings_signal);
    return true;
}
static bool Set(bool value,bool& field,volatile LONG* fast,bool affects_capture=true,bool enrichment_fallback=false) {
    return ChangeSettings([&] {
        if(affects_capture && field!=value) { ++settings.capture_epoch; InterlockedExchange(&capture_epoch,LONG(settings.capture_epoch)); }
        if(&field==&settings.lyrics&&field!=value)++settings.artifact_revision;
        if(enrichment_fallback&&value&&!field)++enrichment_enable_revision;
        field=value;
        if(fast)InterlockedExchange(fast,value);
        return true;
    });
}
bool SetDownloads(bool v) { return Set(v,settings.downloads,&downloads_enabled); }
bool SetOgg(bool v) { return Set(v,settings.ogg,&ogg_enabled); }
bool SetAAC(bool v) { return Set(v,settings.aac,&aac_enabled); }
bool SetFlac(bool v) { return Set(v,settings.flac,&flac_enabled); }
bool SetPodcasts(bool v) { return Set(v,settings.podcasts,nullptr,false); }
bool SetReadSpotifyDiskCache(bool v) { return Set(v,settings.read_spotify_disk_cache,nullptr,false,true); }
bool SetFetchMissingMetadata(bool v) { return Set(v,settings.fetch_missing_metadata,nullptr,false,true); }
bool SetFetchMissingArtwork(bool v) { return Set(v,settings.fetch_missing_artwork,nullptr,false,true); }
bool SetFetchMissingLyrics(bool v) { return Set(v,settings.fetch_missing_lyrics,nullptr,false,true); }
bool SetWindowsPlaybackFallback(bool v) { return Set(v,settings.windows_playback_fallback,nullptr,false); }
bool SetLyrics(bool v) { return Set(v,settings.lyrics,nullptr,false); }
bool SetAutoSkipDownloaded(bool v) { return Set(v,settings.auto_skip_downloaded,nullptr,false); }
static bool SetVisibleToggle(int& setting,bool enabled) {
    return ChangeSettings([&] {
        if(setting<0)return false;
        setting=enabled?1:0;
        return true;
    });
}
bool SetBlockUpdates(bool enabled) { return Set(enabled,settings.block_updates,nullptr,false); }
bool SetMarkedWhenDownloaded(bool enabled) { return Set(enabled,settings.marked_when_downloaded,nullptr,false); }
bool SetNeverPause(bool enabled) { return SetVisibleToggle(settings.never_pause,enabled); }
bool SetNetworkKillSwitch(bool enabled) { return SetVisibleToggle(settings.network_kill_switch,enabled); }
bool SetUpdateExistingMetadata(bool v) { return Set(v,settings.update_existing_metadata,nullptr,false); }
bool SetCaptureSpeed(unsigned speed) {
    if(!CaptureSpeedAvailable(speed))return false;
    return ChangeSettings([&] {
        if(settings.capture_speed!=int(speed)) {
            // Decoded-output pacing does not alter the compressed Ogg/FLAC/AAC
            // bytes already being retained. Keep their capture lifecycle alive;
            // Listen::Observe independently invalidates mixed-speed coverage.
            settings.capture_speed=int(speed);
            InterlockedExchange(&capture_speed_value,LONG(speed));
        }
        return true;
    });
}
bool SetSaveLocation(const std::wstring& root) {
    if(root.empty() || root.size()>2047 || !(root.size()>2 && (root[1]==L':' || root.rfind(L"\\\\",0)==0))) return false;
    DWORD attributes=GetFileAttributesW(WindowsPath(root).c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    if(attributes==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return false;
    auto next_root=root,next_location=root;const bool music=IsMusic(root);
    return ChangeSettings([&] {
        if(settings.root!=next_root||settings.music_folder!=music)++settings.destination_revision;
        settings.root.swap(next_root);
        settings.save_location.swap(next_location);
        settings.music_folder=music;
        return true;
    });
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
