// Runs in a dedicated test directory, never beside an installed client.
#include "../native/history_settings.cpp"
#include <cassert>
namespace history {
void StartLogger() {}
void LogActivity(const char*,const std::string&) {}
void QueueDiagnostic(const char*) {}
}
int main(int argc,char** argv) {
  assert(argc==2);
  assert(history::Settings{}.downloads&&history::Settings{}.update_existing_metadata&&!history::Settings{}.aac);
  assert(!history::AACEnabled());
  const std::string scenario=argv[1];
  wchar_t module[2048];assert(GetModuleFileNameW(nullptr,module,2048));
  std::wstring path=module;path=path.substr(0,path.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
  DeleteFileW(path.c_str());
  if(scenario!="new") {
    assert(WritePrivateProfileStringW(L"To Disk",L"Menu",L"0",path.c_str()));
    if(scenario=="aac-one") assert(WritePrivateProfileStringW(L"To Disk",L"AAC",L"1",path.c_str()));
    if(scenario=="updates-zero") assert(WritePrivateProfileStringW(L"To Disk",L"UpdateExistingMetadata",L"0",path.c_str()));
    if(scenario=="lyrics-zero") assert(WritePrivateProfileStringW(L"To Disk",L"Lyrics",L"0",path.c_str()));
    if(scenario=="zero") assert(WritePrivateProfileStringW(L"To Disk",L"Downloads",L"0",path.c_str()));
    if(scenario=="legacy-zero") assert(WritePrivateProfileStringW(L"History",L"Enabled",L"0",path.c_str()));
  }
  history::InitSettings(nullptr);
  auto s=history::GetSettings();
  assert(s.downloads==(scenario!="zero" && scenario!="legacy-zero"));
  if(scenario=="new") {
    assert(GetPrivateProfileIntW(L"To Disk",L"Downloads",9,path.c_str())==1);
    assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==0);
    assert(GetPrivateProfileIntW(L"To Disk",L"UpdateExistingMetadata",9,path.c_str())==1);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    assert(file!=INVALID_HANDLE_VALUE);unsigned char bom[2]{};DWORD count=0;
    assert(ReadFile(file,bom,2,&count,nullptr)&&count==2&&bom[0]==0xff&&bom[1]==0xfe);CloseHandle(file);
  }
  assert(s.aac==(scenario=="aac-one"));
  assert(history::AACEnabled()==(s.downloads&&s.aac));
  if(s.aac) {
    assert(history::SetDownloads(false)&&!history::AACEnabled());
    assert(history::SetDownloads(s.downloads)&&history::AACEnabled()==s.downloads);
  }
  assert(s.lyrics==(scenario!="lyrics-zero"));
  assert(s.update_existing_metadata==(scenario!="updates-zero"));
  const auto epoch=history::CaptureEpoch();
  assert(history::SetLyrics(false));
  assert(history::CaptureEpoch()==epoch); // Enrichment preferences never discard audio.
  assert(!history::GetSettings().lyrics);
  assert(history::FlushSettings(5000));
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
  assert(history::GetSettings().aac==s.aac);
  assert(GetPrivateProfileIntW(L"To Disk",L"AAC",9,path.c_str())==(s.aac?1:0));
  DeleteFileW(path.c_str());
  puts("PASS: Windows installation defaults and explicit zero preservation");
}
