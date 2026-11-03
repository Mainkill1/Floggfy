// Runs in a dedicated test directory, never beside an installed client.
#include "../native/history_settings.cpp"
#include <cassert>
namespace history {
void StartLogger() {}
void LogEvent(LogKind,const char*,const std::string&) {}
void QueueDiagnostic(const char*) {}
}
int main(int argc,char** argv) {
  assert(argc==2);
  assert(history::Settings{}.downloads&&history::Settings{}.update_existing_metadata&&!history::Settings{}.auto_skip_downloaded&&!history::Settings{}.aac&&!history::Settings{}.podcasts&&!history::Settings{}.windows_playback_fallback&&history::Settings{}.capture_speed==1&&history::Settings{}.capture_speed_limit==0&&history::Settings{}.log_level==1);
  assert(!history::AACEnabled());
  assert(!history::PodcastsEnabled());
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
    if(scenario=="fallback-blank") assert(fallback_fixture(L""));
    else if(scenario=="fallback-leading-space") assert(fallback_fixture(L" 1"));
    else if(scenario=="fallback-trailing-space") assert(fallback_fixture(L"1 "));
    else if(scenario=="fallback-tab") assert(fallback_fixture(L"\t1"));
    else if(scenario=="fallback-word") assert(fallback_fixture(L"true"));
    else if(scenario=="fallback-duplicate") assert(fallback_fixture(L"1",L"1"));
    else assert(WritePrivateProfileStringW(L"To Disk",L"Menu",L"0",path.c_str()));
    if(scenario=="aac-one") assert(WritePrivateProfileStringW(L"To Disk",L"AAC",L"1",path.c_str()));
    if(scenario=="podcasts-one") assert(WritePrivateProfileStringW(L"To Disk",L"Podcasts",L"1",path.c_str()));
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
      const wchar_t* speed=scenario=="speed-2-limit-3"?L"2":scenario=="speed-4-limit-3"?L"4":L"10";
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
  assert(s.downloads==(scenario!="zero" && scenario!="legacy-zero"));
  const auto expected_speed=scenario=="speed-10-limit-10"||scenario=="speed-10-limit-over"?10u:
    scenario=="speed-2-limit-3"?2u:1u;
  const auto expected_limit=scenario=="speed-10-limit-10"||scenario=="speed-10-limit-over"?10u:
    scenario=="speed-2-limit-3"||scenario=="speed-4-limit-3"?3u:0u;
  assert(s.capture_speed==expected_speed);
  assert(s.capture_speed_limit==expected_limit&&history::CaptureSpeedLimit()==expected_limit);
  assert(history::CaptureSpeedAvailable(1));
  assert(!history::CaptureSpeedAvailable(2));
  history::SetCaptureAccelerationAvailable(true);
  assert(history::CaptureSpeedAvailable(2)==(expected_limit>=2));
  assert(history::CaptureSpeedAvailable(4)==(expected_limit>=4));
  assert(history::CaptureSpeedAvailable(8)==(expected_limit>=8));
  assert(history::CaptureSpeedAvailable(10)==(expected_limit>=10));
  assert(!history::CaptureSpeedAvailable(3));
  history::SetCaptureAccelerationAvailable(false);
  assert(s.windows_playback_fallback==(scenario=="fallback-one"));
  const auto expected_log=scenario=="log-0"||scenario=="legacy-log-zero"?0u:
    scenario=="log-2"?2u:scenario=="log-4"||scenario=="legacy-debug-one"?4u:1u;
  assert(s.log_level==expected_log&&history::LogLevel()==expected_log);
  assert(history::CaptureSpeed()==s.capture_speed);
  if(scenario=="new") {
    assert(GetPrivateProfileIntW(L"To Disk",L"Downloads",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"Podcasts",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"CaptureSpeedLimit",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"Windows Playback Fallback",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"AutoSkipDownloaded",9,path.c_str())==0);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);unsigned char bom[2]{};DWORD count=0;
    assert(ReadFile(file,bom,2,&count,nullptr)&&count==2&&bom[0]==0xff&&bom[1]==0xfe);CloseHandle(file);
  }
  assert(s.aac==(scenario=="aac-one"));
  assert(s.podcasts==(scenario=="podcasts-one"));
  assert(history::AACEnabled()==(s.downloads&&s.aac));
  assert(history::PodcastsEnabled()==(s.downloads&&s.podcasts));
  if(s.podcasts) {
    const auto podcast_epoch=history::CaptureEpoch();
    assert(history::SetPodcasts(false)&&!history::PodcastsEnabled());
    assert(history::CaptureEpoch()==podcast_epoch+1);
    assert(history::SetPodcasts(true)&&history::PodcastsEnabled()==s.downloads);
  }
  if(s.aac) {
    assert(history::SetDownloads(false)&&!history::AACEnabled());
    assert(history::SetDownloads(s.downloads)&&history::AACEnabled()==s.downloads);
  }
  assert(s.lyrics==(scenario!="lyrics-zero"));
  assert(s.update_existing_metadata==(scenario!="updates-zero"));
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
  assert(GetPrivateProfileIntW(L"To Disk",L"LogLevel",99,path.c_str())==expected_log);
  assert(GetPrivateProfileIntW(L"To Disk",L"CaptureSpeed",0,path.c_str())==s.capture_speed);
  assert(GetPrivateProfileIntW(L"To Disk",L"Lyrics",9,path.c_str())==0);
  assert(history::SetLyrics(true) && history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"To Disk",L"Lyrics",9,path.c_str())==1);
  assert(history::GetSettings().update_existing_metadata==(scenario!="updates-zero"));
  assert(GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",9,path.c_str())==(scenario=="updates-zero"?0:1));
  const auto generation=history::GetSettings().generation;
  assert(history::SetUpdateExistingMetadata(!s.update_existing_metadata));
  assert(history::CaptureEpoch()==epoch&&history::GetSettings().generation>generation);
  assert(history::SetUpdateExistingMetadata(s.update_existing_metadata)&&history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",9,path.c_str())==(scenario=="updates-zero"?0:1));
  const auto auto_skip_generation=history::GetSettings().generation;
  assert(history::SetAutoSkipDownloaded(!s.auto_skip_downloaded));
  assert(history::CaptureEpoch()==epoch&&history::GetSettings().generation>auto_skip_generation);
  assert(history::SetAutoSkipDownloaded(s.auto_skip_downloaded)&&history::FlushSettings(5000));
  assert(GetPrivateProfileIntW(L"To Disk",L"AutoSkipDownloaded",9,path.c_str())==(s.auto_skip_downloaded?1:0));
  assert(history::GetSettings().aac==s.aac);
  assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==(s.aac?1:0));
  assert(GetPrivateProfileIntW(L"To Disk",L"Podcasts",9,path.c_str())==(s.podcasts?1:0));
  assert(GetPrivateProfileIntW(L"To Disk",L"Windows Playback Fallback",9,path.c_str())==(s.windows_playback_fallback?1:0));
  std::wstring persisted;bool unicode=false;assert(history::settings_file::Read(path,persisted,unicode));
  assert(!history::settings_file::Find(persisted,L"Log").present);
  assert(!history::settings_file::Find(persisted,L"DebugLog").present);
  const std::wstring fallback_key=L"Windows Playback Fallback=";
  const auto fallback_at=persisted.find(fallback_key);assert(fallback_at!=std::wstring::npos);
  assert(persisted.find(fallback_key,fallback_at+fallback_key.size())==std::wstring::npos);
  if(scenario=="fallback-duplicate") {
    auto value=history::settings_file::Find(persisted,L"Windows Playback Fallback");
    assert(value.present&&!value.duplicate&&value.value==L"0");
    assert(history::SetWindowsPlaybackFallback(true)&&history::FlushSettings(5000));
    assert(history::settings_file::Read(path,persisted,unicode));
    value=history::settings_file::Find(persisted,L"Windows Playback Fallback");
    assert(value.present&&!value.duplicate&&value.value==L"1");
    history::Settings reloaded;assert(!history::ReadWindowsPlaybackFallback(reloaded,path)&&reloaded.windows_playback_fallback);
  }
  DeleteFileW(path.c_str());
  puts("PASS: Windows installation defaults and explicit zero preservation");
}
