// Runs in a dedicated test directory, never beside an installed client.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <string>
namespace {
std::wstring competing_path;
bool competing_directory = false;
BOOL CreateDirectoryWithRacer(LPCWSTR path, LPSECURITY_ATTRIBUTES security) {
  if (competing_path == path) {
    competing_path.clear();
    if (competing_directory) {
      assert(CreateDirectoryW(path, nullptr));
    } else {
      HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      assert(file != INVALID_HANDLE_VALUE && CloseHandle(file));
    }
  }
  return CreateDirectoryW(path, security);
}
}
#define CreateDirectoryW CreateDirectoryWithRacer
#include "../native/history_settings.cpp"
#undef CreateDirectoryW
namespace history {
void StartLogger() {}
void LogEvent(LogKind,const char*,const std::string&) {}
void QueueDiagnostic(const char*) {}
}
int main(int argc,char** argv) {
  assert(std::string(history::default_ini).find("DumpTelemetry=0")!=std::string::npos&&
      "blocked payload recording must be an explicit opt-in");
  assert(std::string(history::default_ini).find("SaveUnmatched=1")!=std::string::npos&&
      "first-time setup must preserve unmatched recordings by default");
  assert(std::string(history::default_ini).find("[Matcher]\r\n")!=std::string::npos&&
      std::string(history::default_ini).find("TimeOffsetToleranceMs=10000")!=std::string::npos&&
      std::string(history::default_ini).find("MaxMatchQueue=5")!=std::string::npos&&
      std::string(history::default_ini).find("TimingToleranceSeconds")==std::string::npos&&
      "matcher timing defaults must use milliseconds and expose retirement count");
  for(const auto& fixture:std::array<std::pair<std::wstring,bool>,4>{{
      {L"",true},{L"SaveUnmatched=0\r\n",false},{L"SaveUnmatched=1\r\n",true},
      {L"SaveUnmatched=1\r\nSaveUnmatched=0\r\n",false}}}) {
      history::Settings target;history::ReadRestartOptionsText(target,L"[To Disk]\r\n"+fixture.first);
      assert(target.save_unmatched==fixture.second);
  }
  for(const auto& fixture:std::array<std::pair<std::wstring,bool>,6>{{
      {L"",false},{L"BlockTelemetry=0\r\n",false},{L"BlockTelemetry=1\r\n",true},
      {L"BlockTelemetry=01\r\n",false},{L"BlockTelemetry=2\r\n",false},
      {L"BlockTelemetry=1\r\nBlockTelemetry=1\r\n",false}}}) {
      history::Settings target;target.metadata=false;target.downloads=false;
      history::ReadRestartOptionsText(target,L"[To Disk]\r\n"+fixture.first);
      assert(target.block_telemetry==fixture.second);
  }
  for(const auto& fixture:std::array<std::pair<std::wstring,bool>,6>{{
      {L"",false},{L"DumpTelemetry=0\r\n",false},{L"DumpTelemetry=1\r\n",true},
      {L"DumpTelemetry=01\r\n",false},{L"DumpTelemetry=2\r\n",false},
      {L"DumpTelemetry=1\r\nDumpTelemetry=1\r\n",false}}}) {
      history::Settings target;target.metadata=false;target.downloads=false;
      history::ReadRestartOptionsText(target,L"[To Disk]\r\n"+fixture.first);
      assert(target.dump_telemetry==fixture.second);
  }
  {
      history::Settings target;
      history::ReadRestartOptionsText(target,L"[To Disk]\nBlockTelemetry=1\nDumpTelemetry=1\n[Telemetry]\nBlockTelemetry=0\nDumpTelemetry=0\n");
      assert(!target.block_telemetry&&!target.dump_telemetry);
  }
  assert(std::string(history::default_ini).find("MetadataCacheReadMiB=256")!=std::string::npos);
  assert(argc==2);
  assert(history::Settings{}.downloads&&history::Settings{}.update_existing_metadata&&!history::Settings{}.auto_skip_downloaded&&!history::Settings{}.aac&&history::Settings{}.podcasts&&!history::Settings{}.windows_playback_fallback&&history::Settings{}.capture_speed==-1&&history::Settings{}.log_level==1);
  assert(history::Settings{}.never_pause==-1&&!history::Settings{}.dump_streams);
  for(const auto& fixture:std::array<std::pair<std::wstring,bool>,6>{{
      {L"",false},{L"DumpStreams=0\r\n",false},{L"DumpStreams=1\r\n",true},
      {L"DumpStreams=01\r\n",false},{L"DumpStreams=2\r\n",false},
      {L"DumpStreams=1\r\nDumpStreams=1\r\n",false}}}) {
      history::Settings target;history::ReadRestartOptionsText(target,L"[To Disk]\r\n"+fixture.first);
      assert(target.dump_streams==fixture.second);
  }
  assert(!history::AACEnabled());
  assert(history::DownloadsEnabled());
  const std::string scenario=argv[1];
  wchar_t module[2048];assert(GetModuleFileNameW(nullptr,module,2048));
  std::wstring path=module;path=path.substr(0,path.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
  DeleteFileW(path.c_str());
  if(scenario!="new") {
    const auto fallback_fixture=[&](const wchar_t* value,const wchar_t* duplicate=nullptr) {
      std::wstring text=L"[To Disk]\r\nMenu=0\r\nWindows Playback Fallback=";
      text+=value;text+=L"\r\n";
      if(duplicate){text+=L"Windows Playback Fallback=";text+=duplicate;text+=L"\r\n";}
      return history::settings_file::Write(path,text,true);
    };
    if(scenario=="fallback-options") assert(history::settings_file::Write(path,
      L"[To Disk]\r\nFetchMissingArtwork=1\r\n[Fallbacks]\r\nReadSpotifyDiskCache=0\r\n"
      L"FetchMissingMetadata=1\r\nFetchMissingArtwork=0\r\nFetchMissingLyrics=0\r\n",true));
    if(scenario=="fallback-blank") assert(fallback_fixture(L""));
    else if(scenario=="fallback-leading-space") assert(fallback_fixture(L" 1"));
    else if(scenario=="fallback-trailing-space") assert(fallback_fixture(L"1 "));
    else if(scenario=="fallback-tab") assert(fallback_fixture(L"\t1"));
    else if(scenario=="fallback-word") assert(fallback_fixture(L"true"));
    else if(scenario=="fallback-duplicate") assert(fallback_fixture(L"1",L"1"));
    else if(scenario=="never-pause-duplicate") assert(history::settings_file::Write(path,
      L"[To Disk]\r\nMenu=0\r\nNeverPause=1\r\nNeverPause=1\r\n",true));
    else if(scenario=="log-duplicate") assert(history::settings_file::Write(path,
      L"[To Disk]\r\nMenu=0\r\nLogLevel=4\r\nLogLevel=0\r\n",true));
    else assert(WritePrivateProfileStringW(L"To Disk",L"Menu",L"0",path.c_str()));
    if(scenario=="blocking-legacy-one"||scenario=="blocking-legacy-zero"||scenario=="blocking-precedence") {
      assert(WritePrivateProfileStringW(L"To Disk",L"BlockTelemetry",scenario=="blocking-legacy-zero"?L"0":L"1",path.c_str()));
      assert(WritePrivateProfileStringW(L"To Disk",L"DumpTelemetry",scenario=="blocking-legacy-one"?L"1":L"0",path.c_str()));
      if(scenario=="blocking-precedence")assert(WritePrivateProfileStringW(L"Telemetry",L"BlockTelemetry",L"0",path.c_str()));
    }
    if(scenario=="blocking-category-zero") {
      for(const auto& flag:history::blocking_flags)
        assert(WritePrivateProfileStringW(L"Telemetry",flag.key,flag.member==&history::Settings::block_telemetry?L"1":L"0",path.c_str()));
    }
    if(scenario=="aac-one") assert(WritePrivateProfileStringW(L"To Disk",L"AAC",L"1",path.c_str()));
    if(scenario=="podcasts-one") assert(WritePrivateProfileStringW(L"To Disk",L"Podcasts",L"1",path.c_str()));
    if(scenario=="podcasts-zero") assert(WritePrivateProfileStringW(L"To Disk",L"Podcasts",L"0",path.c_str()));
    if(scenario=="metadata-cache-custom") assert(WritePrivateProfileStringW(L"To Disk",L"MetadataCacheReadMiB",L"512",path.c_str()));
    if(scenario=="metadata-cache-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"MetadataCacheReadMiB",L"invalid",path.c_str()));
    if(scenario=="metadata-cache-zero") assert(WritePrivateProfileStringW(L"To Disk",L"MetadataCacheReadMiB",L"0",path.c_str()));
    if(scenario=="metadata-cache-over") assert(WritePrivateProfileStringW(L"To Disk",L"MetadataCacheReadMiB",L"4097",path.c_str()));
    if(scenario=="buffer-1024") assert(WritePrivateProfileStringW(L"History",L"MaxBufferedMiB",L"1024",path.c_str()));
    if(scenario=="buffer-over") assert(WritePrivateProfileStringW(L"History",L"MaxBufferedMiB",L"2048",path.c_str()));
    if(scenario=="buffer-under") assert(WritePrivateProfileStringW(L"History",L"MaxBufferedMiB",L"1",path.c_str()));
    if(scenario=="never-pause-hidden") assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"-1",path.c_str()));
    if(scenario=="never-pause-off") assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"0",path.c_str()));
    if(scenario=="never-pause-on") assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"1",path.c_str()));
    if(scenario=="never-pause-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"2",path.c_str()));
    if(scenario=="never-pause-padded") assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"01",path.c_str()));
    if(scenario=="auto-skip-one") assert(WritePrivateProfileStringW(L"To Disk",L"AutoSkipDownloaded",L"1",path.c_str()));
    if(scenario=="auto-skip-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"AutoSkipDownloaded",L"2",path.c_str()));
    if(scenario=="auto-skip-padded") assert(WritePrivateProfileStringW(L"To Disk",L"AutoSkipDownloaded",L"01",path.c_str()));
    if(scenario=="log-0") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"0",path.c_str()));
    if(scenario=="log-1") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"1",path.c_str()));
    if(scenario=="log-2") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"2",path.c_str()));
    if(scenario=="log-4") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"4",path.c_str()));
    if(scenario=="log-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"3",path.c_str()));
    if(scenario=="log-padded") assert(WritePrivateProfileStringW(L"To Disk",L"LogLevel",L"01",path.c_str()));
    if(scenario=="legacy-log-zero") assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"0",path.c_str()));
    if(scenario=="legacy-debug-one") {
      assert(WritePrivateProfileStringW(L"To Disk",L"Log",L"1",path.c_str()));
      assert(WritePrivateProfileStringW(L"To Disk",L"DebugLog",L"1",path.c_str()));
    }
    if(scenario=="updates-zero") assert(WritePrivateProfileStringW(L"To Disk",L"UpdateExistingMetadata",L"0",path.c_str()));
    if(scenario=="lyrics-zero") assert(WritePrivateProfileStringW(L"To Disk",L"Lyrics",L"0",path.c_str()));
    if(scenario=="zero") assert(WritePrivateProfileStringW(L"To Disk",L"Downloads",L"0",path.c_str()));
    if(scenario=="legacy-zero") assert(WritePrivateProfileStringW(L"History",L"Enabled",L"0",path.c_str()));
    if(scenario.rfind("speed-",0)==0) {
      const wchar_t* speed=scenario=="speed-zero"?L"0":scenario=="speed-2-limit-3"?L"2":scenario=="speed-4-limit-3"?L"4":L"10";
      assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeed",speed,path.c_str()));
    }
    if(scenario=="speed-10-limit-10") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"10",path.c_str()));
    if(scenario=="speed-10-limit-over") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"99",path.c_str()));
    if(scenario=="speed-2-limit-3"||scenario=="speed-4-limit-3") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"3",path.c_str()));
    if(scenario=="speed-10-limit-zero") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"0",path.c_str()));
    if(scenario=="speed-10-limit-one") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"1",path.c_str()));
    if(scenario=="speed-10-limit-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"fast",path.c_str()));
    if(scenario=="speed-10-limit-padded") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeedLimit",L"010",path.c_str()));
    if(scenario=="speed-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeed",L"3",path.c_str()));
    if(scenario=="speed-padded") assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeed",L"01",path.c_str()));
    if(scenario=="fallback-one") assert(WritePrivateProfileStringW(L"To Disk",L"Windows Playback Fallback",L"1",path.c_str()));
    if(scenario=="fallback-zero") assert(WritePrivateProfileStringW(L"To Disk",L"Windows Playback Fallback",L"0",path.c_str()));
    if(scenario=="fallback-invalid") assert(WritePrivateProfileStringW(L"To Disk",L"Windows Playback Fallback",L"2",path.c_str()));
    if(scenario=="fallback-padded") assert(WritePrivateProfileStringW(L"To Disk",L"Windows Playback Fallback",L"01",path.c_str()));
  }
  history::InitSettings(nullptr);
  auto s=history::GetSettings();
  assert(s.read_spotify_disk_cache==(scenario!="fallback-options"));
  assert(s.fetch_missing_metadata);
  assert(s.fetch_missing_artwork==(scenario!="fallback-options"));
  assert(s.fetch_missing_lyrics==(scenario!="fallback-options"));
  assert(s.metadata_cache_read_mib==(scenario=="metadata-cache-custom"?512u:256u));
  assert(s.block_telemetry==(scenario=="blocking-legacy-one"||scenario=="blocking-category-zero"));
  assert(s.dump_telemetry==(scenario=="blocking-legacy-one"));
  for(const auto& flag:history::blocking_flags)
    if(flag.default_value)assert((s.*(flag.member))==(scenario!="blocking-category-zero"));
  assert(s.downloads==(scenario!="zero" && scenario!="legacy-zero"));
  assert(s.max_buffered_mib==(scenario=="buffer-1024"||scenario=="buffer-over"?1024u:scenario=="buffer-under"?8u:512u));
  const bool speed_shown=scenario.rfind("speed-",0)==0&&scenario!="speed-invalid"&&scenario!="speed-padded";
  const int expected_speed=!speed_shown?-1:scenario=="speed-zero"?0:
    scenario=="speed-2-limit-3"?2:scenario=="speed-4-limit-3"?4:10;
  assert(s.capture_speed==expected_speed);
  assert(history::CaptureSpeedAvailable(1)==speed_shown);
  assert(!history::CaptureSpeedAvailable(2));
  history::SetCaptureAccelerationAvailable(true);
  for(const unsigned speed:{2u,4u,8u,10u})assert(history::CaptureSpeedAvailable(speed)==speed_shown);
  assert(!history::CaptureSpeedAvailable(3));
  history::SetCaptureAccelerationAvailable(false);
  assert(s.windows_playback_fallback==(scenario=="fallback-one"));
  const auto expected_log=scenario=="log-0"?0u:
    scenario=="log-4"?4u:scenario=="log-2"?2u:1u;
  assert(s.log_level==expected_log&&history::LogLevel()==expected_log);
  assert(history::CaptureSpeed()==unsigned(std::max(1,s.capture_speed)));
  if(scenario=="new") {
    const auto parent = std::wstring(module) + L".folders";
    const auto leaf = parent + L"\\child";
    competing_path = history::WindowsPath(leaf);
    assert(!history::EnsureDirectory(leaf));
    assert(competing_path.empty());
    assert(!history::EnsureDirectory(leaf));
    assert(DeleteFileW(leaf.c_str()));
    competing_path = history::WindowsPath(leaf);
    competing_directory = true;
    assert(history::EnsureDirectory(leaf) && history::EnsureDirectory(leaf));
    assert(competing_path.empty());
    assert(RemoveDirectoryW(leaf.c_str()) && RemoveDirectoryW(parent.c_str()));
    assert(GetPrivateProfileIntW(L"To Disk",L"Downloads",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"Podcasts",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"CaptureSpeedLimit",9,path.c_str())==9);
    assert(GetPrivateProfileIntW(L"Fallbacks",L"WindowsPlaybackFallback",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"Misc",L"UpdateExistingMetadata",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"AutoSkipDownloaded",9,path.c_str())==0);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);unsigned char bom[2]{};DWORD count=0;
    assert(ReadFile(file,bom,2,&count,nullptr)&&count==2&&bom[0]==0xff&&bom[1]==0xfe);CloseHandle(file);
  }
  assert(s.aac==(scenario=="aac-one"));
  assert(s.podcasts==(scenario!="podcasts-zero"));
  assert(history::AACEnabled()==(s.downloads&&s.aac));
  if(s.podcasts) {
    const auto podcast_epoch=history::CaptureEpoch();
    assert(history::SetPodcasts(false)&&!history::GetSettings().podcasts);
    // Podcast is a late publication policy. Changing it must not discard an
    // otherwise valid in-flight codec capture before metadata classifies it.
    assert(history::CaptureEpoch()==podcast_epoch);
    assert(history::SetPodcasts(true)&&history::GetSettings().podcasts);
    assert(history::CaptureEpoch()==podcast_epoch);
  }
  if(s.aac) {
    assert(history::SetDownloads(false)&&!history::AACEnabled());
    assert(history::SetDownloads(s.downloads)&&history::AACEnabled()==s.downloads);
  }
  assert(s.lyrics==(scenario!="lyrics-zero"));
  assert(s.update_existing_metadata==(scenario!="updates-zero"));
  const int expected_never_pause=scenario=="never-pause-on"?1:scenario=="never-pause-off"?0:-1;
  assert(s.never_pause==expected_never_pause);
  const auto never_pause_epoch=history::CaptureEpoch();
  const auto never_pause_generation=history::GetSettings().generation;
  if(expected_never_pause<0) {
    assert(!history::SetNeverPause(true)&&history::GetSettings().never_pause==-1);
    assert(history::GetSettings().generation==never_pause_generation);
  } else {
    assert(history::SetNeverPause(expected_never_pause==0));
    assert(history::GetSettings().never_pause==1-expected_never_pause&&
           history::GetSettings().generation>never_pause_generation);
    assert(history::SetNeverPause(expected_never_pause==1));
  }
  assert(history::CaptureEpoch()==never_pause_epoch&&history::FlushSettings(5000));
  assert(int(GetPrivateProfileIntW(L"To Disk",L"NeverPause",99,path.c_str()))==expected_never_pause);
  assert(s.auto_skip_downloaded==(scenario=="auto-skip-one"));
  const auto epoch=history::CaptureEpoch();
  const auto fallback_generation=history::GetSettings().generation;
  assert(history::SetWindowsPlaybackFallback(!s.windows_playback_fallback));
  assert(history::CaptureEpoch()==epoch&&history::GetSettings().generation>fallback_generation);
  assert(history::SetWindowsPlaybackFallback(s.windows_playback_fallback));
  assert(history::SetLyrics(false));
  assert(history::CaptureEpoch()==epoch); // Enrichment preferences never discard audio.
  assert(!history::GetSettings().lyrics);
  assert(history::FlushSettings(5000));
  if(scenario=="new"||scenario=="missing"||scenario=="metadata-cache-custom")
    assert(GetPrivateProfileIntW(L"History",L"MetadataCacheReadMiB",0,path.c_str())==s.metadata_cache_read_mib);
  assert(GetPrivateProfileIntW(L"Debug",L"LogLevel",99,path.c_str())==expected_log);
  assert(int(GetPrivateProfileIntW(L"To Disk",L"CaptureSpeed",0,path.c_str()))==s.capture_speed);
  assert(GetPrivateProfileIntW(L"To Disk",L"Lyrics",9,path.c_str())==0);
  assert(history::SetLyrics(true) && history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"To Disk",L"Lyrics",9,path.c_str())==1);
  assert(history::GetSettings().update_existing_metadata==(scenario!="updates-zero"));
  assert(GetPrivateProfileIntW(L"Misc",L"UpdateExistingMetadata",9,path.c_str())==(scenario=="updates-zero"?0:1));
  const auto generation=history::GetSettings().generation;
  assert(history::SetUpdateExistingMetadata(!s.update_existing_metadata));
  assert(history::CaptureEpoch()==epoch&&history::GetSettings().generation>generation);
  assert(history::SetUpdateExistingMetadata(s.update_existing_metadata)&&history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"Misc",L"UpdateExistingMetadata",9,path.c_str())==(scenario=="updates-zero"?0:1));
  const auto auto_skip_generation=history::GetSettings().generation;
  assert(history::SetAutoSkipDownloaded(!s.auto_skip_downloaded));
  assert(history::CaptureEpoch()==epoch&&history::GetSettings().generation>auto_skip_generation);
  assert(history::SetAutoSkipDownloaded(s.auto_skip_downloaded)&&history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"To Disk",L"AutoSkipDownloaded",9,path.c_str())==(s.auto_skip_downloaded?1:0));
  assert(history::GetSettings().aac==s.aac);
  assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==(s.aac?1:0));
  assert(GetPrivateProfileIntW(L"To Disk",L"Podcasts",9,path.c_str())==(s.podcasts?1:0));
  assert(GetPrivateProfileIntW(L"Fallbacks",L"WindowsPlaybackFallback",9,path.c_str())==(s.windows_playback_fallback?1:0));
  std::wstring persisted;bool unicode=false;assert(history::settings_file::Read(path,persisted,unicode));
  assert(!history::settings_file::Find(persisted,L"BlockTelemetry").present);
  assert(!history::settings_file::Find(persisted,L"DumpTelemetry").present);
  for(const auto& flag:history::blocking_flags) {
    const auto value=history::settings_file::Find(persisted,flag.key,L"[Telemetry]");
    assert(value.present&&!value.duplicate&&value.value==(s.*(flag.member)?L"1":L"0"));
  }
  const auto persisted_log=history::settings_file::Find(persisted,L"LogLevel",L"[Debug]");
  assert(persisted_log.present&&!persisted_log.duplicate&&persisted_log.value==std::to_wstring(expected_log));
  assert(!history::settings_file::Find(persisted,L"Log").present);
  assert(!history::settings_file::Find(persisted,L"DebugLog").present);
  const std::wstring fallback_key=L"WindowsPlaybackFallback=";
  const auto fallback_at=persisted.find(fallback_key);assert(fallback_at!=std::wstring::npos);
  assert(persisted.find(fallback_key,fallback_at+fallback_key.size())==std::wstring::npos);
  if(scenario=="fallback-duplicate") {
    auto value=history::settings_file::Find(persisted,L"WindowsPlaybackFallback",L"[Fallbacks]");
    assert(value.present&&!value.duplicate&&value.value==L"0");
    assert(history::SetWindowsPlaybackFallback(true)&&history::FlushSettings(5000));
    assert(history::settings_file::Read(path,persisted,unicode));
    value=history::settings_file::Find(persisted,L"WindowsPlaybackFallback",L"[Fallbacks]");
    assert(value.present&&!value.duplicate&&value.value==L"1");
    history::Settings reloaded;assert(!history::ReadWindowsPlaybackFallback(reloaded,path)&&reloaded.windows_playback_fallback);
  }
  DeleteFileW(path.c_str());
  puts("PASS: Windows installation defaults and explicit zero preservation");
}
