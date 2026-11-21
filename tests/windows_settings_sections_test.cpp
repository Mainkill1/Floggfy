// Configuration regression fixtures only; no installed Spotify or networking.
#include "../native/history_settings.cpp"
#include <cassert>
#include <iostream>
namespace history {
void StartLogger() {}
void LogEvent(LogKind,const char*,const std::string&) {}
void QueueDiagnostic(const char*) {}
}
int main() {
    using namespace history;
    assert(Settings{}.capture_speed==-1&&Settings{}.network_kill_switch==0&&!Settings{}.block_updates);
    assert(Settings{}.log_level==1&&Settings{}.matcher_duration_ms==25);
    assert(Settings{}.marked_when_downloaded);
    const std::string defaults=default_ini;
    assert(defaults.find("CaptureSpeedLimit")==std::string::npos);
    size_t previous=0;
    for(const char* section:{"[To Disk]","[Fallbacks]","[Debug]","[Matcher]","[History]","[Telemetry]","[Misc]"}) {
        const auto position=defaults.find(section);assert(position!=defaults.npos&&position>=previous);previous=position;
    }
    std::wstring text=L"; preserve my comments\n[To Disk]\nCustom=keep\nWindows Playback Fallback=1\nLogLevel=2\nDebugConnect=0\nDebugNetworking=1\nDebugHookDiagnostics=1\nDumpStreams=1\nMetadataCacheReadMiB=512\nNetworkPatch=1\nCaptureSpeedLimit=0\nCaptureSpeed=4\nUpdateExistingMetadata=0\nTrackNumberInFilename=1\nConvertNumberedFilenames=0\nSaveUnmatched=1\n[Matcher]\nSaveUnmatched=0\n[Debug]\nLogLevel=4\n[Fallbacks]\nWindowsPlaybackFallback=0\n[Other]\nNetworkPatch=keep\n";
    Settings parsed;ReadRestartOptionsText(parsed,text);
    assert(parsed.debug_networking&&parsed.debug_hook_diagnostics&&parsed.dump_streams&&!parsed.debug_connect);
    assert(parsed.network_patch&&parsed.metadata_cache_read_mib==512&&!parsed.save_unmatched);
    InsertMissingRestartDefaults(text);
    assert(text.find(L"; preserve my comments")!=text.npos);
    assert(settings_file::Find(text,L"Custom").value==L"keep");
    assert(settings_file::Find(text,L"NetworkPatch",L"[Other]").value==L"keep");
    assert(settings_file::Find(text,L"LogLevel",L"[Debug]").value==L"4");
    assert(settings_file::Find(text,L"WindowsPlaybackFallback",L"[Fallbacks]").value==L"0");
    assert(settings_file::Find(text,L"MetadataCacheReadMiB",L"[History]").value==L"512");
    assert(settings_file::Find(text,L"SaveUnmatched",L"[Matcher]").value==L"0");
    for(const auto& option:section_options) {
        assert(!settings_file::Find(text,option.key).present);
        if(option.legacy_key)assert(!settings_file::Find(text,option.legacy_key).present);
        assert(settings_file::Find(text,option.key,option.section).present);
    }
    assert(!settings_file::Find(text,L"CaptureSpeedLimit").present);
    assert(settings_file::Find(text,L"MarkedWhenDownloaded",L"[Misc]").value==L"1");
    assert(settings_file::Find(text,L"CaptureSpeed").value==L"4");
    assert(settings_file::Find(text,L"UpdateExistingMetadata",L"[Misc]").value==L"0");
    assert(settings_file::Find(text,L"TrackNumberInFilename",L"[Misc]").value==L"1");
    assert(settings_file::Find(text,L"ConvertNumberedFilenames",L"[Misc]").value==L"0");
    const auto migrated=text;InsertMissingRestartDefaults(text);assert(text==migrated);
    std::wstring duplicates=L"[To Disk]\nDebugNetworking=1\nDebugNetworking=1\n";
    InsertMissingRestartDefaults(duplicates);ReadRestartOptionsText(parsed,duplicates);assert(!parsed.debug_networking);

    wchar_t executable[32768];assert(GetModuleFileNameW(nullptr,executable,32768));
    const std::wstring exe=executable,path=exe.substr(0,exe.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
    DeleteFileW(path.c_str());assert(settings_file::Write(path,L"",true));
    for(const int speed:{-1,0,1,2,4,8,10}) {
        assert(settings_file::Write(path,L"[To Disk]\nCaptureSpeed="+std::to_wstring(speed)+L"\n"));
        assert(!ReadCaptureSpeed(parsed,path)&&parsed.capture_speed==speed);
        InterlockedExchange(&capture_speed_value,speed);SetCaptureAccelerationAvailable(false);
        assert(CaptureSpeed()==unsigned(std::max(1,speed)));
        assert(CaptureSpeedAvailable(1)==(speed>=0)&&!CaptureSpeedAvailable(2));
        SetCaptureAccelerationAvailable(true);assert(CaptureSpeedAvailable(10)==(speed>=0));
    }
    for(const wchar_t* value:{L"",L"-2",L"3",L"11",L"01",L"1\nCaptureSpeed=1"}) {
        assert(settings_file::Write(path,std::wstring(L"[To Disk]\nCaptureSpeed=")+value+L"\n"));
        assert(ReadCaptureSpeed(parsed,path)&&parsed.capture_speed==-1);
    }
    assert(settings_file::Write(path,L"[To Disk]\nTrackNumberInFilename=0\nConvertNumberedFilenames=1\n[Misc]\nTrackNumberInFilename=1\nConvertNumberedFilenames=0\n"));
    assert(!ReadFilenameOptions(parsed,path)&&parsed.track_number_in_filename&&!parsed.convert_numbered_filenames);
    assert(settings_file::Write(path,L"[To Disk]\nTrackNumberInFilename=1\nConvertNumberedFilenames=1\n[Misc]\nTrackNumberInFilename=\nConvertNumberedFilenames=0\n"));
    assert(ReadFilenameOptions(parsed,path)&&!parsed.track_number_in_filename&&!parsed.convert_numbered_filenames);
    assert(settings_file::Write(path,L"[Misc]\nTrackNumberInFilename=1\nConvertNumberedFilenames=1\n"));
    assert(ReadFilenameOptions(parsed,path)&&parsed.track_number_in_filename&&!parsed.convert_numbered_filenames);
    for(const int mode:{-1,0,1}) {
        assert(settings_file::Write(path,L"[Misc]\nNetworkKillSwitch="+std::to_wstring(mode)+L"\n"));
        assert(!ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==mode);
    }
    for(const wchar_t* value:{L"",L"2",L"01",L"+1",L" 1",L"1 ",L"1\nNetworkKillSwitch=1",L"0\nNetworkKillSwitch=1"}) {
        assert(settings_file::Write(path,std::wstring(L"[Misc]\nNetworkKillSwitch=")+value+L"\n"));
        assert(ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==0);
    }
    assert(settings_file::Write(path,L"[Misc]\nOther=1\n"));
    assert(ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==0);
    assert(settings_file::Write(path,L"[To Disk]\nNetworkKillSwitch=1\n"));
    assert(ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==0);
    assert(DeleteFileW(path.c_str()));
    assert(!ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==0);
    assert(settings_file::Write(path,L"[Misc]\nNetworkKillSwitch=1\n",true));
    {
        settings_file::Handle locked(CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));
        assert(locked.value!=INVALID_HANDLE_VALUE);
        assert(!ReadNetworkKillSwitch(parsed,path)&&parsed.network_kill_switch==0);
    }
    for(const bool enabled:{false,true}) {
        assert(settings_file::Write(path,enabled?L"[Misc]\nBlockUpdates=1\n":L"[Misc]\nBlockUpdates=0\n"));
        assert(!ReadBlockUpdates(parsed,path)&&parsed.block_updates==enabled);
    }
    for(const wchar_t* value:{L"",L"2",L"01",L"-1",L"true",L" 1",L"1\nBlockUpdates=1"}) {
        assert(settings_file::Write(path,std::wstring(L"[Misc]\nBlockUpdates=")+value+L"\n"));
        assert(ReadBlockUpdates(parsed,path)&&!parsed.block_updates);
    }
    assert(settings_file::Write(path,L"[To Disk]\nBlockUpdates=1\n"));
    assert(ReadBlockUpdates(parsed,path)&&!parsed.block_updates); // Only Misc enables it.
    assert(DeleteFileW(path.c_str()));assert(CreateDefaultIni(path));
    assert(GetPrivateProfileIntW(L"Misc",L"BlockUpdates",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"Misc",L"NetworkKillSwitch",9,path.c_str())==0);
    for(const wchar_t* key:{L"UpdateExistingMetadata",L"TrackNumberInFilename",L"ConvertNumberedFilenames"}) {
        wchar_t value[32];assert(GetPrivateProfileStringW(L"Misc",key,L"",value,32,path.c_str()));
        assert(!GetPrivateProfileStringW(L"To Disk",key,L"",value,32,path.c_str()));
    }
    assert(WritePrivateProfileStringW(L"Misc",L"BlockUpdates",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"Misc",L"NetworkKillSwitch",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeed",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"Misc",L"UpdateExistingMetadata",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"Misc",L"ConvertNumberedFilenames",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"UpdateExistingMetadata",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"TrackNumberInFilename",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"To Disk",L"ConvertNumberedFilenames",L"0",path.c_str()));
    InitSettings(nullptr);assert(FlushSettings(5000));
    assert(!GetSettings().update_existing_metadata&&!GetSettings().track_number_in_filename&&GetSettings().convert_numbered_filenames);
    assert(SetUpdateExistingMetadata(true)&&SetCaptureSpeed(4)&&FlushSettings(5000));
    std::wstring persisted;bool unicode=false;assert(settings_file::Read(path,persisted,unicode));
    assert(settings_file::Find(persisted,L"CaptureSpeed").value==L"4");
    assert(settings_file::Find(persisted,L"UpdateExistingMetadata",L"[Misc]").value==L"1");
    assert(settings_file::Find(persisted,L"TrackNumberInFilename",L"[Misc]").value==L"0");
    assert(settings_file::Find(persisted,L"ConvertNumberedFilenames",L"[Misc]").value==L"1");
    for(const wchar_t* key:{L"UpdateExistingMetadata",L"TrackNumberInFilename",L"ConvertNumberedFilenames"}) {
        assert(!settings_file::Find(persisted,key).present);
        assert(!settings_file::Find(persisted,key,L"[Misc]").duplicate);
    }
    assert(persisted.find(L"; Fill missing tags, artwork and lyrics in existing recordings.")!=persisted.npos);
    assert(persisted.find(L"; Adds the track number infront of songs")!=persisted.npos);
    assert(persisted.find(L"; Accelerated use may trigger account restrictions.")!=persisted.npos);
    assert(SetCaptureSpeed(1)&&FlushSettings(5000));
    const auto epoch=CaptureEpoch();
    assert(GetSettings().block_updates);
    assert(SetBlockUpdates(false)&&!GetSettings().block_updates&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"BlockUpdates",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"Misc",L"NetworkKillSwitch",9,path.c_str())==0);
    assert(SetBlockUpdates(true)&&GetSettings().block_updates&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"BlockUpdates",9,path.c_str())==1);
    assert(CaptureEpoch()==epoch);
    assert(CaptureSpeed()==1&&CaptureSpeedAvailable(1));
    assert(SetNetworkKillSwitch(true)&&GetSettings().network_kill_switch==1&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"NetworkKillSwitch",9,path.c_str())==1);
    assert(SetNetworkKillSwitch(false)&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"NetworkKillSwitch",9,path.c_str())==0);
    // Fresh restart-only edits survive a runtime menu change and migration.
    assert(WritePrivateProfileStringW(L"Debug",L"LogLevel",L"4",path.c_str()));
    assert(WritePrivateProfileStringW(L"Debug",L"DumpStreams",L"1",path.c_str()));
    assert(WritePrivateProfileStringW(L"Matcher",L"SaveUnmatched",L"0",path.c_str()));
    assert(WritePrivateProfileStringW(L"History",L"MetadataCacheReadMiB",L"1024",path.c_str()));
    assert(SetBlockUpdates(false)&&SetLyrics(false)&&FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Debug",L"LogLevel",9,path.c_str())==4);
    assert(GetPrivateProfileIntW(L"Debug",L"DumpStreams",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"Matcher",L"SaveUnmatched",9,path.c_str())==0&&GetSettings().save_unmatched);
    assert(GetPrivateProfileIntW(L"History",L"MetadataCacheReadMiB",9,path.c_str())==1024);
    assert(GetSettings().log_level==1&&!GetSettings().dump_streams&&GetSettings().metadata_cache_read_mib==256);
    {ExclusiveSrw guard(lock);settings.network_kill_switch=-1;}
    assert(!SetNetworkKillSwitch(true)&&GetSettings().network_kill_switch==-1);
    assert(CaptureEpoch()==epoch);
    assert(DeleteFileW(path.c_str()));
    std::cout<<"PASS: INI categories, legacy migration, signed capture speeds, update/kill-switch settings and external edits\n";
}
