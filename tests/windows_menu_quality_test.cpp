// Exercise the production CEF menu boundary with a model implementing the
// required public prefix. No Spotify binaries or private media are needed.
#include "../native/media/playback_quality_windows.cpp"
#include "../native/tooling/history_settings.h"
#include "../native/tooling/async_log.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cassert>
#include <new>
static thread_local bool fail_menu_allocation=false;
static bool fail_recent_placeholder=false;
void* operator new(std::size_t n){if(fail_menu_allocation){fail_menu_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
static unsigned capability_queries=0;
static SIZE_T CountQuery(LPCVOID p,PMEMORY_BASIC_INFORMATION m,SIZE_T n) {
  ++capability_queries;return VirtualQuery(p,m,n);
}
static unsigned filesystem_calls=0,shell_calls=0,message_calls=0,folder_creations=0,folder_opens=0;
static bool folder_shell_succeeds=true,use_real_file_attributes=false;
static std::wstring opened_folder;
static HINSTANCE WINAPI OpenFolder(HWND,LPCWSTR operation,LPCWSTR file,LPCWSTR,LPCWSTR,INT) {
  assert(std::wstring(operation)==L"open");++folder_opens;opened_folder=file;return reinterpret_cast<HINSTANCE>(folder_shell_succeeds ? 33 : 5);
}
static bool saved_file_exists=true,launch_click_worker=true;
static DWORD saved_file_attributes=FILE_ATTRIBUTE_NORMAL;
static HRESULT parse_result=S_OK,shell_result=S_OK;
static std::wstring last_message;
static std::wstring selected_path,checked_path;
static DWORD WINAPI FileAttributes(LPCWSTR path) {checked_path=path;++filesystem_calls;return use_real_file_attributes ? GetFileAttributesW(path) : saved_file_exists ? saved_file_attributes : INVALID_FILE_ATTRIBUTES;}
static HRESULT WINAPI ParseItem(PCWSTR path,IBindCtx*,PIDLIST_ABSOLUTE* item,SFGAOF,SFGAOF*) {
  if(FAILED(parse_result))return parse_result;
  selected_path=path;*item=static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(ITEMIDLIST)));return S_OK;
}
static HRESULT WINAPI ShowItem(PCIDLIST_ABSOLUTE,UINT count,PCUITEMID_CHILD_ARRAY,DWORD) {
  assert(count==0);++shell_calls;return shell_result;
}
static int WINAPI ShowMessage(HWND,LPCWSTR text,LPCWSTR,UINT) {
  ++message_calls;last_message=text;return IDOK;
}
static HANDLE WINAPI RunClickWorker(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE entry,LPVOID value,DWORD,LPDWORD) {
  if(!launch_click_worker)return nullptr;
  auto thread=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(thread)entry(value);
  return thread;
}
static bool network_available=true,network_enabled=false,network_change_succeeds=true;
static bool network_setting_succeeds=true;
static unsigned network_set_calls=0,network_fail_on_call=0;
namespace network {
bool RequestReconnect(bool) noexcept {assert(false);return false;} // Live-only cases opt out below.
bool Available() {return network_available;}
bool Enabled() {return network_enabled;}
bool SetEnabled(bool enabled) {
  ++network_set_calls;
  if(!network_available||!network_change_succeeds||network_set_calls==network_fail_on_call)return false;
  network_enabled=enabled;return true;
}
}
static bool updates_available=true,updates_blocked=false,updates_change_succeeds=true,updates_setting_succeeds=true;
static unsigned updates_set_calls=0,updates_fail_on_call=0;
static DWORD updates_error=ERROR_SUCCESS;
namespace updates {
bool Available(){return updates_available;}
bool Blocked(){return updates_blocked;}
DWORD LastError(){return updates_error;}
bool SetBlocked(bool blocked){
  ++updates_set_calls;
  if(!updates_available||!updates_change_succeeds||updates_set_calls==updates_fail_on_call){updates_error=ERROR_ACCESS_DENIED;return false;}
  updates_blocked=blocked;updates_error=ERROR_SUCCESS;return true;
}
}
static std::vector<std::string> menu_log_lines;
namespace history {void LogMenuEvent(LogKind kind,const char* event,const std::string& detail){menu_log_lines.push_back(detail);LogEvent(kind,event,detail);}}
#define LogEvent LogMenuEvent
static unsigned location_picker_calls=0;
namespace history {bool SetBlockUpdatesForMenuTest(bool);bool SetNetworkKillSwitchForMenuTest(bool);void PickSaveLocationForMenuTest(HWND){++location_picker_calls;}}
#define PickSaveLocation PickSaveLocationForMenuTest
#define SetNetworkKillSwitch SetNetworkKillSwitchForMenuTest
#define SetBlockUpdates SetBlockUpdatesForMenuTest
#define GetFileAttributesW FileAttributes
#define SHParseDisplayName ParseItem
#define SHOpenFolderAndSelectItems ShowItem
#define MessageBoxW ShowMessage
#define CreateThread RunClickWorker
#define ShellExecuteW OpenFolder
#define EnsureDirectory EnsureSongDirectoryForMenuTest
#define ReadSongDestination ReadSongDestinationForMenuTest
#define ReadRecentDownloads ReadRecentDownloadsForMenuTest
#define VirtualQuery CountQuery
#include "../native/ui/to_disk_menu.cpp"
#undef LogEvent
#undef PickSaveLocation
#undef SetNetworkKillSwitch
#undef SetBlockUpdates
#undef VirtualQuery
#undef GetFileAttributesW
#undef SHParseDisplayName
#undef SHOpenFolderAndSelectItems
#undef MessageBoxW
#undef CreateThread
#undef ShellExecuteW
#undef EnsureDirectory
#undef ReadSongDestination
#undef ReadRecentDownloads
static bool recent_ready=true;
static unsigned recent_reads=0;
static std::vector<history::RecentDownload> published_recent;
static std::uint64_t published_song_id=0;
static unsigned published_song_generation=0;
static std::wstring published_song_file;
static void SetSongFile(std::uint64_t identity,const std::wstring& file) {
  published_song_id=identity;published_song_file=file;published_song_generation=history::GetSettings().generation;
}
namespace history {
bool SetBlockUpdatesForMenuTest(bool enabled){return updates_setting_succeeds&&SetBlockUpdates(enabled);}
bool SetNetworkKillSwitchForMenuTest(bool enabled) {return network_setting_succeeds&&SetNetworkKillSwitch(enabled);}
bool ReadSongDestinationForMenuTest(std::uint64_t identity,unsigned generation,std::wstring& file) {
  if(identity!=published_song_id || generation!=published_song_generation || published_song_file.empty())return false;
  file=published_song_file;return true;
}
bool EnsureSongDirectoryForMenuTest(const std::wstring&) {++folder_creations;return true;}
bool ReadRecentDownloadsForMenuTest(std::vector<RecentDownload>& out) {
  ++recent_reads;if(!recent_ready)return false;out=published_recent;return true;
}
}
#include <algorithm>
#include <cassert>
#include <memory>
#include <map>
#include <vector>
struct Entry {
  int id = 0, type = 1;
  std::wstring label;
  bool enabled = true, checked = false, visible = true;
};
struct Model {
  Menu api{};
  bool submenu = false;
  int refs = 1;
  std::vector<Entry> entries;
  std::unique_ptr<Model> child;
  int child_id = 0;
  std::map<int,std::unique_ptr<Model>> extra_children;
};
static Model &M(Menu *m) { return *reinterpret_cast<Model *>(m); }
static void Retain(Base *b) { ++reinterpret_cast<Model *>(b)->refs; }
static int Drop(Base *b) { return --reinterpret_cast<Model *>(b)->refs == 0; }
static int OneRef(Base *b) { return reinterpret_cast<Model *>(b)->refs == 1; }
static int AnyRef(Base *b) { return reinterpret_cast<Model *>(b)->refs > 0; }
static int IsSub(Menu *m) { return M(m).submenu; }
static int Clear(Menu *m) {
  M(m).entries.clear();
  return 1;
}
static size_t Count(Menu *m) { return M(m).entries.size(); }
static unsigned type_calls=0,index_calls=0;
static int Index(Menu *m, int id) {
  ++index_calls;
  auto &list = M(m).entries;
  auto it = std::find_if(list.begin(), list.end(), [&](const auto &i) { return i.id == id; });
  return it == list.end() ? -1 : int(it - list.begin());
}
static int Separator(Menu *m) {
  M(m).entries.push_back({-1, 4, L"", false, false});
  return 1;
}
static int Item(Menu *m, int id, const String *s) {
  M(m).entries.push_back({id, 1, std::wstring(s->str, s->length), true, false});
  return 1;
}
static int CheckItem(Menu *m, int id, const String *s) {
  Item(m, id, s);
  M(m).entries.back().type = 2;
  return 1;
}
static int Label(Menu *m, int id, const String *s) {
  int i = Index(m, id);
  assert(i >= 0);
  M(m).entries[size_t(i)].label.assign(s->str, s->length);
  return 1;
}
static int Type(Menu *m, size_t index) {
  ++type_calls;
  assert(index < M(m).entries.size());
  return M(m).entries[index].type;
}
static int Enable(Menu *m, int id, int value) {
  int i = Index(m, id);
  assert(i >= 0);
  M(m).entries[size_t(i)].enabled = value;
  return 1;
}
static int Visible(Menu *m,int id,int value) {
  int i=Index(m,id);assert(i>=0);M(m).entries[size_t(i)].visible=value!=0;return 1;
}
static int Check(Menu *m, int id, int value) {
  int i = Index(m, id);
  assert(i >= 0);
  M(m).entries[size_t(i)].checked = value;
  return 1;
}
static Menu *Child(Menu *m, int id) {
  if (Index(m, id) < 0)return nullptr;
  Model* child=M(m).child_id==id?M(m).child.get():nullptr;
  if(!child) {auto found=M(m).extra_children.find(id);if(found!=M(m).extra_children.end())child=found->second.get();}
  if(!child)return nullptr;
  Retain(&child->api.base);return &child->api;
}
static void Init(Model &model);
static Menu *AddChild(Menu *m, int id, const String *s) {
  Item(m, id, s);
  M(m).entries.back().type = 5;
  auto next=std::make_unique<Model>();Init(*next);next->submenu=true;Model* result=next.get();
  if(!M(m).child){M(m).child_id=id;M(m).child=std::move(next);}
  else M(m).extra_children[id]=std::move(next);
  Retain(&result->api.base);
  if(id==recent_id&&fail_recent_placeholder){fail_recent_placeholder=false;fail_menu_allocation=true;}
  return &result->api;
}
static void Init(Model &model) {
  model.api.base = {sizeof(Menu), Retain, Drop, OneRef, AnyRef};
  auto &f = model.api.methods;
  f[0] = reinterpret_cast<void *>(IsSub);
  f[1] = reinterpret_cast<void *>(Clear);
  f[2] = reinterpret_cast<void *>(Count);
  f[3] = reinterpret_cast<void *>(Separator);
  f[4] = reinterpret_cast<void *>(Item);
  f[5] = reinterpret_cast<void *>(CheckItem);
  f[7] = reinterpret_cast<void *>(AddChild);
  f[15] = reinterpret_cast<void *>(Index);
  f[20] = reinterpret_cast<void *>(Label);
  f[23] = reinterpret_cast<void *>(Type);
  f[28] = reinterpret_cast<void *>(Child);
  f[32] = reinterpret_cast<void *>(Visible);
  f[36] = reinterpret_cast<void *>(Enable);
  f[40] = reinterpret_cast<void *>(Check);
}
static void FreeText(wchar_t* text) { delete[] text; }
static int SetText(const wchar_t* source,size_t length,String* output,int copy) {
  assert(copy==1);
  auto value=new wchar_t[length+1];std::copy_n(source,length,value);value[length]=0;
  if(output->dtor)output->dtor(output->str);
  *output={value,length,FreeText};return 1;
}
static int original_commands = 0;
static void OriginalCommand(Delegate *, Menu *menu, int, int) {
  ++original_commands;
  ReleaseMenu(menu);
}
static int format_calls=0;
static int OriginalFormat(Delegate*,Menu* menu,String*) {++format_calls;ReleaseMenu(menu);return 0;}
static int ReleaseDelegate(Base *) { return 0; }
#ifdef FLOGGFY_SONG_SHORTCUT_TEST
int RunMenuQualityTests() {
  const bool never_pause_hidden=false;
#else
int main(int argc,char** argv) {
  assert(argc==1||(argc==2&&std::string(argv[1])=="--never-pause-hidden"));
  const bool never_pause_hidden=argc==2;
#endif
  char setting_line[96]{};
  SettingOutcomeLine(podcasts_id,true,true,setting_line,sizeof(setting_line));
  assert(std::string(setting_line)=="Podcasts enabled");
  SettingOutcomeLine(lyrics_id,true,false,setting_line,sizeof(setting_line));
  assert(std::string(setting_line)=="Lyrics disabled");
  SettingOutcomeLine(lyrics_id,false,true,setting_line,sizeof(setting_line));
  assert(std::string(setting_line)=="Lyrics change failed; remains enabled");
  SettingOutcomeLine(auto_skip_id,true,true,setting_line,sizeof(setting_line));
  assert(std::string(setting_line)=="Auto Skip Downloaded enabled");
  assert(menu_init.TryBegin(GetTickCount64()));menu_prepared=true;
  FinishMenuStartup(false,false);
  assert(menu_init.State()==hooks::InitState::RetryableFailure);
  // A failed application can race a detour entry before its callback counter.
  menu_init.Retry(0);assert(menu_init.TryBegin(GetTickCount64()));menu_prepared=true;
  FinalizeToDiskMenu(false);
  assert(menu_init.State()==hooks::InitState::Unsupported);
  wchar_t module[2048];assert(GetModuleFileNameW(nullptr,module,2048));
  std::wstring ini=module;ini=ini.substr(0,ini.find_last_of(L'\\'))+L"\\SpotifyHistory.ini";
  DeleteFileW(ini.c_str());
  history::Settings speed_opt_out;speed_opt_out.capture_speed=-1;
  assert(!ShowCaptureSpeed(speed_opt_out));
  history::Settings speed_default;
  assert(!ShowCaptureSpeed(speed_default));
  speed_opt_out.capture_speed=0;assert(ShowCaptureSpeed(speed_opt_out));
  speed_opt_out.capture_speed=1;assert(ShowCaptureSpeed(speed_opt_out));
  Model default_speed;Init(default_speed);
  default_speed.entries={{1,5,L"First"},{2,5,L"Second"},{3,5,L"Third"}};
  Populate(&default_speed.api);
  assert(default_speed.child);
  assert(Index(&default_speed.child->api,28471)<0);
  auto& default_misc=*default_speed.child->extra_children.at(misc_root_id);
  assert(default_misc.entries.size()==6);
  const auto default_kill_index=Index(&default_misc.api,network_kill_id);
  assert(default_kill_index>=0&&!default_misc.entries[default_kill_index].checked);
  assert(default_speed.child->extra_children.at(fallbacks_root_id)->entries.size()==5);
  const auto default_speed_child=default_speed.child->extra_children.find(speed_root_id);
  assert(default_speed_child==default_speed.child->extra_children.end());
  assert(WritePrivateProfileStringW(L"To Disk",L"CaptureSpeed",L"0",ini.c_str()));
  assert(WritePrivateProfileStringW(L"Misc",L"NetworkKillSwitchTelemetryLock",L"0",ini.c_str()));
  if(never_pause_hidden)assert(WritePrivateProfileStringW(L"Misc",L"NetworkKillSwitch",L"-1",ini.c_str()));
  assert(WritePrivateProfileStringW(L"To Disk",L"Podcasts",L"0",ini.c_str()));
  const auto test_log_root=ini.substr(0,ini.find_last_of(L'\\'))+L"\\Menu-test-logs";
  assert(WritePrivateProfileStringW(L"To Disk",L"Save Location",test_log_root.c_str(),ini.c_str()));
  if(!never_pause_hidden)assert(WritePrivateProfileStringW(L"To Disk",L"NeverPause",L"0",ini.c_str()));
  history::InitSettings(nullptr);
  assert(history::GetSettings().root==test_log_root);
  assert(history::SetLyrics(true));
  assert(history::GetSettings().capture_speed==0&&history::CaptureSpeed()==1);
  history::PlaybackQualitySnapshot initial;
  initial.rate = 44100;
  history::PublishPlaybackQuality(initial);
  assert(history::ReadPlaybackQuality().rate == 44100);
  AcquireSRWLockExclusive(&history::snapshot_lock);
  const auto before = GetTickCount64();
  assert(history::ReadPlaybackQuality().rate == 0);
  assert(GetTickCount64() - before < 100); // A menu must not wait for the publisher.
  ReleaseSRWLockExclusive(&history::snapshot_lock);
  history::updated = GetTickCount64() - 4000;
  assert(history::ReadPlaybackQuality().rate == 0); // Stale worker snapshots are not current.
  Model failed_population;Init(failed_population);
  failed_population.entries={{1,5,L"First"},{2,5,L"Second"},{3,5,L"Third"}};
  fail_recent_placeholder=true;bool allocation_threw=false;
  try {Populate(&failed_population.api);}catch(const std::bad_alloc&){allocation_threw=true;}
  assert(allocation_threw&&failed_population.child&&failed_population.child->child);
  assert(failed_population.child->refs==1&&failed_population.child->child->refs==1);
  Model root;
  Init(root);
  root.entries = {{1, 5, L"First"}, {2, 5, L"Second"}, {3, 5, L"Third"}};
  history::PlaybackQualitySnapshot q;
  std::copy_n(L"Example", 7, q.title.begin());
  q.format = history::PlaybackFormat::Ogg;
  q.rate = 44100;
  q.level = history::PlaybackLevel::VeryHigh;
  history::PublishPlaybackQuality(q);
  Populate(&root.api);
  assert(root.child);
  auto &child = *root.child;
  assert(root.entries.back().label==L"To Disk");
  assert(child.entries[0].id==project_id&&child.entries[0].label==std::wstring(L"Floggfy - ")+FLOGGFY_VERSION_W);
  assert(child.entries[0].type==1&&!child.entries[0].enabled&&!child.entries[0].checked);
  assert(child.entries[1].type==4&&child.entries[2].id==downloads_id);
  assert(Index(&child.api,recent_id)==Index(&child.api,downloads_id)+1);
  assert(child.entries[Index(&child.api,28490)].label==L"Latest Downloads");
  assert(!child.entries[Index(&child.api,recent_id)].visible);
  assert(child.child && child.child->refs==1);
  assert(!child.child->entries[0].enabled);
  assert(filesystem_calls==0 && shell_calls==0 && recent_reads==0);
  auto& formats=*child.extra_children.at(formats_root_id);
  auto& misc=*child.extra_children.at(misc_root_id);
  auto& fallbacks=*child.extra_children.at(fallbacks_root_id);
  assert(formats.entries.size()==5&&formats.refs==1);
  assert(formats.entries[0].id==ogg_id&&formats.entries[1].id==flac_id&&formats.entries[2].id==aac_id&&formats.entries[3].id==podcasts_id);
  assert(!formats.entries[2].checked&&!formats.entries[3].checked);
  assert(formats.entries[2].label==L"AAC"&&formats.entries[3].label==L"Podcasts");
  assert(misc.entries.size()==(never_pause_hidden?5u:6u)&&misc.refs==1);
  assert(misc.entries[0].id==location_id&&misc.entries[0].type==1);
  assert(misc.entries[1].id==auto_skip_id&&!misc.entries[1].checked);
  assert(misc.entries[4].id==block_updates_id&&misc.entries[4].enabled&&!misc.entries[4].checked&&misc.entries[4].label==L"Block Updates");
  assert(formats.entries.back().id==lyrics_id&&formats.entries.back().checked&&formats.entries.back().label==L"Include Lyrics");
  assert(Index(&misc.api,lyrics_id)<0);
  assert(fallbacks.entries.size()==5&&fallbacks.refs==1);
  assert(fallbacks.entries[0].label==L"Read Spotify Disk Cache"&&fallbacks.entries[0].checked);
  assert(fallbacks.entries[1].label==L"Fetch Missing Metadata"&&fallbacks.entries[1].checked);
  assert(fallbacks.entries[2].label==L"Fetch Missing Artwork"&&fallbacks.entries[2].checked);
  assert(fallbacks.entries[3].label==L"Fetch Missing Lyrics"&&fallbacks.entries[3].checked);
  assert(fallbacks.entries[4].id==windows_fallback_id&&!fallbacks.entries[4].checked);
  assert((Index(&misc.api,network_kill_id)<0)==never_pause_hidden);
  for(int id:{location_id,ogg_id,flac_id,aac_id,podcasts_id,lyrics_id,auto_skip_id,windows_fallback_id,block_updates_id})assert(Index(&child.api,id)<0);
  assert(child.entries.size()==(never_pause_hidden?13u:14u));
  const auto speed_child=child.extra_children.find(speed_root_id);
  assert(speed_child!=child.extra_children.end()&&speed_child->second->entries.size()==5);
  assert(speed_child->second->entries[0].checked);
  const auto quality_index=size_t(Index(&child.api,song_id));
  for (size_t i = quality_index; i < quality_index+4; i++)
    assert(child.entries[i].type == 1 && !child.entries[i].enabled && !child.entries[i].checked);
  assert(child.entries[quality_index].label.find(L"Example") != std::wstring::npos);
  assert(child.entries[quality_index].visible && child.entries[quality_index+1].visible);
  assert(child.entries[quality_index+2].visible && child.entries[quality_index+3].visible);
  type_calls=0;
  Populate(&root.api);
  assert(type_calls==0); // Existing insertion is not rescanned or repopulated.
  assert(std::count_if(child.entries.begin(),child.entries.end(),[](const Entry& entry){return entry.id==project_id;})==1);
  assert(child.entries[0].id==project_id&&child.entries[1].type==4&&child.entries[2].id==downloads_id);
  assert(child.entries.size() == (never_pause_hidden?13u:14u) && child.refs == 1);
  capability_queries=type_calls=index_calls=0;
  original_add_submenu=AddChild;
  Model unrelated;Init(unrelated);unrelated.submenu=true;
  std::wstring sublabel=L"Other";auto subtext=Text(sublabel);
  auto* added=AddSubmenuHook(&unrelated.api,91,&subtext);ReleaseMenu(added);
  assert(type_calls==0 && index_calls==0 && capability_queries==0);
  const auto construction_start=GetTickCount64();
  for(unsigned i=0;i<10000;++i) Populate(&root.api);
  assert(type_calls==0 && index_calls==10000 && capability_queries==0);
  std::printf("BENCH: 10000 repeated construction callbacks %llu ms; type scans %u; probes %u\n",
              GetTickCount64()-construction_start,type_calls,capability_queries);
  // Main-menu recognition uses types, never translated labels or mnemonics.
  const wchar_t* localized[][3]={
    {L"ファイル",L"編集",L"表示"},
    {L"&Datei",L"&Bearbeiten",L"&Ansicht"},
    {L"ملف",L"تحرير",L"عرض"}
  };
  for(const auto& labels:localized) {
    Model translated;Init(translated);
    translated.entries={{1,5,labels[0]},{2,5,labels[1]},{3,5,labels[2]}};
    Populate(&translated.api);
    assert(translated.child && translated.entries.size()==4);
    assert(translated.entries.back().label==L"To Disk");
    assert(translated.child->entries.size()==(never_pause_hidden?13u:14u) && translated.child->refs==1);
    Populate(&translated.api);
    assert(translated.entries.size()==4 && translated.child->entries.size()==(never_pause_hidden?13u:14u));
  }
  // The client changes first while SMTC still publishes the previous song.
  history::PlaybackQualitySnapshot actual;
  actual.identity=history::PlaybackIdentity("Next\x1f" "Artist\x1f" "Album");
  std::copy_n(L"Next",4,actual.title.begin());
  actual.level=history::PlaybackLevel::Lossless;
  history::PublishClientPlaybackQuality(actual);
  assert(history::ReadPlaybackQuality().title[0]==L'N');
  assert(history::ReadPlaybackQuality().format==history::PlaybackFormat::Unknown);
  // Publishing the old SMTC snapshot again must not restore its title or codec.
  history::PublishPlaybackQuality(q);
  assert(history::ReadPlaybackQuality().identity==actual.identity);
  q.identity=actual.identity;
  q.title=actual.title;
  q.format = history::PlaybackFormat::Flac;
  q.rate = 96000;
  q.bits = 16;
  history::PublishPlaybackQuality(q);
  actual.level=history::PlaybackLevel::Lossless24;
  history::PublishClientPlaybackQuality(actual);
  assert(history::ReadPlaybackQuality().format==history::PlaybackFormat::Unknown);
  actual.level=history::PlaybackLevel::Lossless;
  history::PublishClientPlaybackQuality(actual);
  q.level = history::PlaybackLevel::Lossless;
  history::PublishPlaybackQuality(q);
  Delegate original{};
  original.base.release = ReleaseDelegate;
  original.execute = OriginalCommand;
  original.format = OriginalFormat;
  Wrapped wrapped;
  wrapped.original = &original;
  const auto messages_before_heading=message_calls,folders_before_heading=folder_opens;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,project_id,0);
  assert(message_calls==messages_before_heading&&folder_opens==folders_before_heading);
  assert(child.refs==1&&original_commands==0&&!child.entries[0].enabled);
  Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,location_id,0);
  assert(location_picker_calls==1&&misc.refs==1);
  const auto fallback_epoch=history::CaptureEpoch();
  Retain(&fallbacks.api.base);Execute(&wrapped.api,&fallbacks.api,windows_fallback_id,0);
  assert(history::GetSettings().windows_playback_fallback&&fallbacks.entries[4].checked&&fallbacks.refs==1);
  Retain(&fallbacks.api.base);Execute(&wrapped.api,&fallbacks.api,windows_fallback_id,0);
  assert(!history::GetSettings().windows_playback_fallback&&!fallbacks.entries[4].checked&&history::CaptureEpoch()==fallback_epoch);
  const wchar_t* fallback_keys[]={L"ReadSpotifyDiskCache",L"FetchMissingMetadata",L"FetchMissingArtwork",L"FetchMissingLyrics"};
  for(unsigned i=0;i<4;++i) {
    const int command=fallbacks.entries[i].id;
    Retain(&fallbacks.api.base);Execute(&wrapped.api,&fallbacks.api,command,0);
    assert(!fallbacks.entries[i].checked&&fallbacks.refs==1&&history::CaptureEpoch()==fallback_epoch);
    assert(history::FlushSettings(5000)&&GetPrivateProfileIntW(L"Fallbacks",fallback_keys[i],9,ini.c_str())==0);
    Retain(&fallbacks.api.base);Execute(&wrapped.api,&fallbacks.api,command,0);
    assert(fallbacks.entries[i].checked&&history::CaptureEpoch()==fallback_epoch);
    assert(history::FlushSettings(5000)&&GetPrivateProfileIntW(L"Fallbacks",fallback_keys[i],9,ini.c_str())==1);
  }
  assert(!history::AACEnabled());
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,aac_id,0);
  assert(history::GetSettings().aac&&history::AACEnabled()&&formats.entries[2].checked&&formats.entries[2].label==L"AAC"&&formats.refs==1);
  assert(history::FlushSettings(5000)&&GetPrivateProfileIntW(L"To Disk",L"AAC",0,ini.c_str())==1);
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,aac_id,0);
  assert(!history::GetSettings().aac&&!history::AACEnabled()&&!formats.entries[2].checked&&formats.refs==1);
  // Misc controls persist the feature flag and show scan state when reused.
  {
    auto click_marked=[&](int id){Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,id,0);assert(misc.refs==1);};
    assert(history::GetSettings().marked_when_downloaded);
    assert(misc.entries[Index(&misc.api,marked_id)].label==L"Marked When Downloaded");
    assert(misc.entries[Index(&misc.api,marked_rebuild_id)].label==L"Marked DB Rebuild");
    click_marked(marked_rebuild_id);
    assert(history::MarkedRebuildRunning());
    assert(misc.entries[Index(&misc.api,marked_rebuild_id)].checked);
    click_marked(marked_id);
    assert(!history::GetSettings().marked_when_downloaded&&!history::MarkedRebuildRunning());
    assert(!misc.entries[Index(&misc.api,marked_rebuild_id)].enabled);
    assert(history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"MarkedWhenDownloaded",9,ini.c_str())==0);
    click_marked(marked_id);
    click_marked(marked_rebuild_id);
    assert(history::ToggleMarkedRebuild()); // Background completion/cancel before reopened menu.
    Retain(&misc.api.base);WillShow(&wrapped.api,&misc.api);
    assert(!misc.entries[Index(&misc.api,marked_rebuild_id)].checked);
    assert(history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"MarkedWhenDownloaded",9,ini.c_str())==1);
  }
  for(int id:{formats_root_id,misc_root_id,fallbacks_root_id}){Retain(&child.api.base);Execute(&wrapped.api,&child.api,id,0);}
  assert(original_commands==0&&child.refs==1);
  {
    auto click_updates=[&]{Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,block_updates_id,0);assert(misc.refs==1);};
    auto& entry=misc.entries[Index(&misc.api,block_updates_id)];
    const auto epoch=history::CaptureEpoch();
    click_updates();
    assert(updates_blocked&&history::GetSettings().block_updates&&entry.checked&&entry.label==L"Block Updates");
    assert(history::CaptureEpoch()==epoch&&history::FlushSettings(5000));
    assert(GetPrivateProfileIntW(L"Misc",L"BlockUpdates",0,ini.c_str())==1);
    click_updates();assert(!updates_blocked&&!history::GetSettings().block_updates&&!entry.checked);
    updates_change_succeeds=false;
    const auto messages_before=message_calls;
    click_updates();
    assert(!updates_blocked&&!history::GetSettings().block_updates&&!entry.checked&&message_calls==messages_before+1);
    assert(updates_error==ERROR_ACCESS_DENIED);
    assert(std::find(menu_log_lines.begin(),menu_log_lines.end(),"Block Updates failed stage=apply error=5 active=0")!=menu_log_lines.end());
    updates_change_succeeds=true;updates_setting_succeeds=false;
    const auto calls_before=updates_set_calls;
    click_updates();
    assert(updates_set_calls==calls_before+2&&!updates_blocked&&!history::GetSettings().block_updates&&!entry.checked);
    updates_fail_on_call=updates_set_calls+2;
    click_updates();
    assert(updates_blocked&&!history::GetSettings().block_updates&&entry.checked); // Failed rollback must show actual protection.
    assert(std::find(menu_log_lines.begin(),menu_log_lines.end(),"Block Updates failed stage=rollback error=5 active=1")!=menu_log_lines.end());
    updates_fail_on_call=0;updates_setting_succeeds=true;
    click_updates();assert(!updates_blocked&&!entry.checked);
    updates_available=false;updates_error=ERROR_PATH_NOT_FOUND;
    Model unavailable;Init(unavailable);unavailable.entries={{1,5,L"First"},{2,5,L"Second"},{3,5,L"Third"}};
    Populate(&unavailable.api);
    auto& unavailable_misc=*unavailable.child->extra_children.at(misc_root_id);
    const auto& unavailable_entry=unavailable_misc.entries[Index(&unavailable_misc.api,block_updates_id)];
    assert(!unavailable_entry.enabled&&!unavailable_entry.checked&&unavailable_entry.label==L"Block Updates (Unavailable)");
    const auto calls_unavailable=updates_set_calls;
    click_updates();
    assert(updates_set_calls==calls_unavailable&&!updates_blocked&&!history::GetSettings().block_updates);
    assert(std::find(menu_log_lines.begin(),menu_log_lines.end(),"Block Updates failed stage=unavailable error=3 active=0")!=menu_log_lines.end());
    updates_available=true;updates_error=ERROR_SUCCESS;
    assert(history::CaptureEpoch()==epoch);
  }
  if(never_pause_hidden){
    const auto messages_before=message_calls,calls_before=network_set_calls;
    Retain(&child.api.base);Execute(&wrapped.api,&child.api,network_kill_id,0);
    assert(history::GetSettings().network_kill_switch==-1&&message_calls==messages_before&&network_set_calls==calls_before);
  }
  if(!never_pause_hidden){
    auto click_kill=[&]{Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,network_kill_id,0);assert(misc.refs==1);};
    const auto epoch=history::CaptureEpoch();
    click_kill();
    assert(network_enabled&&history::GetSettings().network_kill_switch==1&&misc.entries[Index(&misc.api,network_kill_id)].checked);
    assert(history::CaptureEpoch()==epoch);
    click_kill();
    assert(!network_enabled&&history::GetSettings().network_kill_switch==0&&!misc.entries[Index(&misc.api,network_kill_id)].checked);
    network_change_succeeds=false;
    click_kill();
    assert(!network_enabled&&history::GetSettings().network_kill_switch==0&&!misc.entries[Index(&misc.api,network_kill_id)].checked);
    network_change_succeeds=true;network_setting_succeeds=false;
    const auto calls_before=network_set_calls;
    click_kill();
    assert(network_set_calls==calls_before+2); // Apply succeeded, rejected persistence rolls back.
    assert(!network_enabled&&history::GetSettings().network_kill_switch==0&&!misc.entries[Index(&misc.api,network_kill_id)].checked);
    network_fail_on_call=network_set_calls+2;
    click_kill();
    assert(network_enabled&&history::GetSettings().network_kill_switch==0&&misc.entries[Index(&misc.api,network_kill_id)].checked); // Reflect actual protection if rollback fails.
    network_fail_on_call=0;network_setting_succeeds=true;
    click_kill();assert(!network_enabled&&!misc.entries[Index(&misc.api,network_kill_id)].checked);
    network_available=false;
    Model unavailable;Init(unavailable);unavailable.entries={{1,5,L"First"},{2,5,L"Second"},{3,5,L"Third"}};
    Populate(&unavailable.api);
    auto& unavailable_misc=*unavailable.child->extra_children.at(misc_root_id);
    const auto& unavailable_kill=unavailable_misc.entries[Index(&unavailable_misc.api,network_kill_id)];
    assert(!unavailable_kill.enabled&&!unavailable_kill.checked&&unavailable_kill.label==L"Network Kill Switch (Unavailable)");
    const auto calls_while_unavailable=network_set_calls;
    click_kill();
    assert(network_set_calls==calls_while_unavailable&&!network_enabled&&history::GetSettings().network_kill_switch==0);
    network_available=true;
  }
  history::SetCaptureAccelerationAvailable(true);
  auto& speed_menu=*child.extra_children.at(speed_root_id);
  const auto capture_epoch=history::CaptureEpoch();
  Retain(&child.api.base);WillShow(&wrapped.api,&child.api);
  for(const auto& entry:speed_menu.entries)assert(entry.enabled);
  const auto messages_before_acceleration=message_calls;
  Retain(&speed_menu.api.base);Execute(&wrapped.api,&speed_menu.api,first_speed_id+3,0);
  assert(history::CaptureSpeed()==8&&message_calls==messages_before_acceleration);
  Retain(&speed_menu.api.base);Execute(&wrapped.api,&speed_menu.api,first_speed_id+2,0);
  assert(history::CaptureSpeed()==4&&history::CaptureEpoch()==capture_epoch&&speed_menu.refs==1);
  for(unsigned i=0;i<5;++i)assert(speed_menu.entries[i].checked==(i==2));
  const auto messages_while_active=message_calls;
  Retain(&speed_menu.api.base);Execute(&wrapped.api,&speed_menu.api,first_speed_id+3,0);
  assert(history::CaptureSpeed()==8&&message_calls==messages_while_active);
  // Native fallback can change speed/capability while CEF retains this model.
  history::SetCaptureAccelerationAvailable(false);assert(history::SetCaptureSpeed(1));
  Retain(&child.api.base);WillShow(&wrapped.api,&child.api);
  assert(speed_menu.entries[0].checked&&speed_menu.entries[0].enabled);
  for(unsigned i=1;i<5;++i)assert(!speed_menu.entries[i].checked&&!speed_menu.entries[i].enabled);
  const auto messages_before_unsupported=message_calls;
  Retain(&speed_menu.api.base);Execute(&wrapped.api,&speed_menu.api,first_speed_id+2,0);
  assert(history::CaptureSpeed()==1&&message_calls==messages_before_unsupported+1);
  assert(last_message.find(L"unavailable")!=std::wstring::npos);
  history::SetCaptureAccelerationAvailable(true);
  // CEF builds visible labels BEFORE WillShow. FormatLabel is the public
  // callback that can update the first opening without an extra hook.
  set_string=SetText;
  std::wstring old_label=L"Song: Unavailable";auto rendered=Text(old_label);
  Retain(&child.api.base);
  assert(Format(&wrapped.api,&child.api,&rendered)==1);
  assert(std::wstring(rendered.str,rendered.length)==L"Song: Next");
  rendered.dtor(rendered.str);
  assert(format_calls==1 && child.refs==1);
  capability_queries=0;
  const auto format_start=GetTickCount64();
  for(unsigned i=0;i<10000;++i) {
    old_label=L"Song: Unavailable";rendered=Text(old_label);
    Retain(&child.api.base);
    assert(Format(&wrapped.api,&child.api,&rendered)==1);
    rendered.dtor(rendered.str);
  }
  assert(capability_queries==0); // Validated function table reused without retaining the model.
  std::printf("BENCH: 10000 quality callbacks %llu ms; capability probes %u\n",
              GetTickCount64()-format_start,capability_queries);
  format_calls=1;
  std::wstring untouched=L"Save Location";rendered=Text(untouched);
  Retain(&child.api.base);
  capability_queries=0;
  assert(Format(&wrapped.api,&child.api,&rendered)==0);
  assert(capability_queries==0 && recent_reads==0); // Unrelated labels do no capability probing or history reads.
  assert(rendered.str==untouched.data() && format_calls==2 && child.refs==1);
  auto setter=set_string;set_string=nullptr;
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);
  assert(Format(&wrapped.api,&child.api,&rendered)==0);
  assert(rendered.str==old_label.data() && child.refs==1);
  set_string=setter;
  Retain(&child.api.base);
  WillShow(&wrapped.api, &child.api);
  assert(child.entries[quality_index+2].label.find(L"FLAC") != std::wstring::npos &&
         child.entries[quality_index+3].label.find(L"96,000 Hz") != std::wstring::npos);
  assert(child.entries[quality_index].label==L"Song: Next");
  assert(child.entries[quality_index+2].visible && child.entries[quality_index+3].visible); // Confirmed FLAC details remain.
  // Refresh the submenu before CEF builds its visible children, even when
  // changing codec while the same model is reused across menu openings.
  auto build_root=[&]{std::wstring label=L"To Disk";auto text=Text(label);Retain(&root.api.base);Format(&wrapped.api,&root.api,&text);if(text.dtor)text.dtor(text.str);};
  history::Catalog destination_catalog;destination_catalog.title=L"Next";destination_catalog.artist=L"Album Artist";destination_catalog.album=L"Album";
  build_root();
  assert(child.entries[Index(&child.api,song_id)].visible && !child.entries[Index(&child.api,song_id)].enabled);
  const auto fs_before_disabled_click=filesystem_calls;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(filesystem_calls==fs_before_disabled_click && folder_creations==0 && folder_opens==0);
  SetSongFile(actual.identity,history::OutputPath(L"C:\\Saved",destination_catalog,L".flac",false));
  // The parent already froze this label while its destination check was
  // pending. Opening the child can pick up its now-ready RAM result.
  published_song_generation=history::GetSettings().generation+1;
  Retain(&child.api.base);WillShow(&wrapped.api,&child.api);
  assert(!child.entries[Index(&child.api,song_id)].enabled);
  SetSongFile(actual.identity+99,L"C:\\Other\\wrong.flac");
  Retain(&child.api.base);WillShow(&wrapped.api,&child.api);
  assert(!child.entries[Index(&child.api,song_id)].enabled);
  SetSongFile(actual.identity,history::OutputPath(L"C:\\Saved",destination_catalog,L".flac",false));
  const auto same_label=child.entries[Index(&child.api,song_id)].label;
  Retain(&child.api.base);WillShow(&wrapped.api,&child.api);
  assert(child.entries[Index(&child.api,song_id)].enabled&&child.entries[Index(&child.api,song_id)].label==same_label);
  build_root();
  assert(child.entries[Index(&child.api,song_id)].visible && child.entries[Index(&child.api,song_id)].enabled);
  assert(folder_creations==0 && folder_opens==0);
  // Parent formatting starts an opening snapshot. A renderer update between
  // parent and Song formatting must not redirect that opening's label or target.
  auto before_show=actual;before_show.identity+=77;before_show.title.fill(0);std::copy_n(L"Before show",11,before_show.title.begin());
  history::PublishClientPlaybackQuality(before_show);history::PublishPlaybackQuality(before_show);
  SetSongFile(before_show.identity,L"C:\\Changed-before-show\\song.flac");
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);Format(&wrapped.api,&child.api,&rendered);
  assert(std::wstring(rendered.str,rendered.length)==L"Song: Next");rendered.dtor(rendered.str);
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  SetSongFile(actual.identity,history::OutputPath(L"C:\\Saved",destination_catalog,L".flac",false));
  assert(!child.entries[Index(&child.api,recent_id)].visible);
  published_recent={{L"Newest song",L"C:\\Music\\newest.flac",42},
                    {L"Earlier song",L"C:\\Music\\earlier.ogg",17}};
  build_root();
  auto& recent=*child.child;
  assert(child.entries[Index(&child.api,recent_id)].visible);
  assert(recent.entries.size()==2 && recent.entries[0].label==L"Newest song");
  assert(recent.entries[1].label==L"Earlier song" && recent.entries[1].id==first_recent_id+1);
  assert(recent.refs==1 && filesystem_calls==0 && shell_calls==0);
  Retain(&root.api.base);WillShow(&wrapped.api,&root.api);
  assert(wrapped.opening.Visible() && root.refs==1);
  const auto reads_while_open=recent_reads;
  published_recent={{L"Arrived while menu open",L"C:\\Music\\different.flac",8}};
  build_root();assert(recent_reads==reads_while_open && recent.entries[0].label==L"Newest song");
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(selected_path==L"C:\\Music\\newest.flac" && shell_calls==1 && recent.refs==1);
  Retain(&root.api.base);Closed(&wrapped.api,&root.api);
  assert(!wrapped.opening.Visible() && root.refs==1);
  recent_ready=false;build_root();
  assert(recent.entries.size()==2 && recent.entries[0].label==L"Newest song");
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id+1,0);
  assert(selected_path==L"C:\\Music\\earlier.ogg" && shell_calls==2 && recent.refs==1);
  saved_file_exists=false;
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(shell_calls==2 && message_calls==messages_before_unsupported+2 && original_commands==0 && recent.refs==1);
  saved_file_exists=true;recent_ready=true;
  build_root();assert(recent.entries.size()==1 && recent.entries[0].label==L"Arrived while menu open");
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(selected_path==L"C:\\Music\\different.flac" && shell_calls==3);
  published_recent.clear();
  for(unsigned i=0;i<12;++i)published_recent.push_back({L"Song "+std::to_wstring(i),L"C:\\Music\\"+std::to_wstring(i)+L".flac",i});
  build_root();assert(recent.entries.size()==10 && recent.entries.back().id==28500);
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,28500,0);
  assert(selected_path==L"C:\\Music\\9.flac" && shell_calls==4);
  published_recent.clear();build_root();
  assert(!child.entries[Index(&child.api,recent_id)].visible && recent.entries.size()==1 && !recent.entries[0].enabled);
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(shell_calls==4 && original_commands==0 && recent.refs==1);
  const std::wstring unsafe_title=std::wstring(L"A&B\tLine\n")+wchar_t(0)+wchar_t(127)+wchar_t(0x85)+L"Tail";
  const std::wstring split_pair=std::wstring(94,L'界')+wchar_t(0xd83d)+wchar_t(0xde00)+L"suffix";
  const std::wstring intact_pair=std::wstring(93,L'界')+wchar_t(0xd83d)+wchar_t(0xde00)+L"suffix";
  published_recent={{unsafe_title,L"C:\\Music\\escaped.flac",1},
                    {split_pair,L"C:\\Music\\split.flac",2},
                    {intact_pair,L"C:\\Music\\intact.flac",3},
                    {std::wstring(100,L'&'),L"C:\\Music\\ampersands.flac",4},
                    {L"",L"C:\\Music\\untitled.flac",5},
                    {split_pair.substr(0,96),L"C:\\Music\\exact.flac",6}};
  const auto fs_before_labels=filesystem_calls;
  build_root();
  assert(recent.entries[0].label==L"A&&B Line    Tail");
  assert(recent.entries[1].label==std::wstring(94,L'界')+L"\u2026");
  assert(recent.entries[2].label==intact_pair.substr(0,95)+L"\u2026");
  assert(recent.entries[3].label==std::wstring(190,L'&')+L"\u2026");
  assert(recent.entries[4].label==L"Untitled download" && wrapped.opening.snapshot.recent[4].title.empty());
  assert(recent.entries[5].label==split_pair.substr(0,96));
  assert(wrapped.opening.snapshot.recent[0].title==unsafe_title && wrapped.opening.snapshot.recent[1].title==split_pair);
  assert(filesystem_calls==fs_before_labels && shell_calls==4 && recent.refs==1);
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(selected_path==L"C:\\Music\\escaped.flac" && shell_calls==5 && recent.refs==1);
  const auto long_path=L"C:\\Music\\"+std::wstring(280,L'a')+L".flac";
  published_recent={{L"Long saved path",long_path,8}};build_root();
  Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);
  assert(checked_path==history::WindowsPath(long_path));
  assert(selected_path==long_path && shell_calls==6);
  // Deleted files/folders and shell failures never dispatch an unrelated path.
  auto click_recent=[&]{Retain(&recent.api.base);Execute(&wrapped.api,&recent.api,first_recent_id,0);assert(recent.refs==1);};
  const auto missing_notice=L"This download is no longer available at its saved location.";
  saved_file_exists=false;click_recent();assert(last_message==missing_notice && shell_calls==6);
  published_recent={{L"Deleted folder",L"C:\\Music\\Removed folder\\song.flac",8}};build_root();
  click_recent();assert(last_message==missing_notice && shell_calls==6);
  saved_file_exists=true;saved_file_attributes=FILE_ATTRIBUTE_DIRECTORY;click_recent();
  assert(last_message==missing_notice && shell_calls==6);
  saved_file_attributes=FILE_ATTRIBUTE_NORMAL;parse_result=E_FAIL;click_recent();
  assert(last_message==L"Explorer could not show this download." && shell_calls==6);
  parse_result=S_OK;shell_result=E_FAIL;click_recent();
  assert(last_message==L"Explorer could not show this download." && shell_calls==7);
  shell_result=S_OK;launch_click_worker=false;click_recent();
  assert(last_message==L"Explorer could not be started." && shell_calls==7);
  launch_click_worker=true;
  const auto intended_folder=history::OutputPath(L"C:\\Saved",destination_catalog,L".flac",false);
  const auto expected_folder=intended_folder.substr(0,intended_folder.find_last_of(L'\\'));
  Retain(&root.api.base);WillShow(&wrapped.api,&root.api);
  auto next_song=actual;next_song.identity=actual.identity+1;
  next_song.title.fill(0);std::copy_n(L"Changed",7,next_song.title.begin());
  history::PublishClientPlaybackQuality(next_song);history::PublishPlaybackQuality(next_song);
  destination_catalog.album=L"New Album";
  SetSongFile(next_song.identity,history::OutputPath(L"C:\\NewRoot",destination_catalog,L".ogg",false));
  build_root();
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);Format(&wrapped.api,&child.api,&rendered);
  assert(std::wstring(rendered.str,rendered.length)==L"Song: Next");rendered.dtor(rendered.str);
  assert(child.entries[Index(&child.api,song_id)].label==L"Song: Next");
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_creations==0 && folder_opens==1 && opened_folder==expected_folder);
  assert(checked_path==history::WindowsPath(intended_folder));
  const auto messages_before_song_failure=message_calls;
  saved_file_exists=false;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  const auto missing_song_notice=L"This song's saved audio is no longer available at its saved location.";
  assert(folder_creations==0 && folder_opens==1 && message_calls==messages_before_song_failure+1 && last_message==missing_song_notice);
  saved_file_exists=true;saved_file_attributes=FILE_ATTRIBUTE_DIRECTORY;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_creations==0 && folder_opens==1 && last_message==missing_song_notice);
  saved_file_attributes=FILE_ATTRIBUTE_NORMAL;folder_shell_succeeds=false;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_creations==0 && folder_opens==2 && last_message==L"Explorer could not open this song's folder.");
  folder_shell_succeeds=true;launch_click_worker=false;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_creations==0 && folder_opens==2 && last_message==L"The song folder could not be opened.");
  launch_click_worker=true;
  Retain(&root.api.base);Closed(&wrapped.api,&root.api);
  build_root();assert(child.entries[Index(&child.api,song_id)].label==L"Song: Changed");
  const auto changed_file=history::OutputPath(L"C:\\NewRoot",destination_catalog,L".ogg",false);
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_creations==0 && folder_opens==3 && opened_folder==changed_file.substr(0,changed_file.find_last_of(L'\\')));
  const auto original_save_root=history::GetSettings().root;
  wchar_t destination_temp[2048];GetTempPathW(2048,destination_temp);
  assert(history::SetSaveLocation(std::wstring(destination_temp)+L"Floggfy-song-location-change-"+std::to_wstring(GetCurrentProcessId())));
  build_root();assert(child.entries[Index(&child.api,song_id)].visible && !child.entries[Index(&child.api,song_id)].enabled);
  const auto clicks_before_location_change=filesystem_calls;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(filesystem_calls==clicks_before_location_change);
  assert(history::SetSaveLocation(original_save_root));
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  // An old or mismatched worker snapshot can never enable a new song's target.
  build_root();assert(child.entries[Index(&child.api,song_id)].visible && !child.entries[Index(&child.api,song_id)].enabled);
  const auto clicks_before_unavailable=filesystem_calls;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(filesystem_calls==clicks_before_unavailable && folder_opens==3);
  // Exercise the click-time boundary against actual Windows files as well as
  // mocks: deletion after binding must not open or recreate an empty folder.
  const auto real_folder=std::wstring(destination_temp)+L"Floggfy-song-deleted-"+std::to_wstring(GetCurrentProcessId());
  assert(CreateDirectoryW(real_folder.c_str(),nullptr));
  const auto real_file=real_folder+L"\\Saved.ogg";
  HANDLE real_audio=CreateFileW(real_file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
  assert(real_audio!=INVALID_HANDLE_VALUE);DWORD real_written=0;
  assert(WriteFile(real_audio,"saved",5,&real_written,nullptr) && real_written==5);CloseHandle(real_audio);
  SetSongFile(actual.identity,real_file);build_root();
  assert(child.entries[Index(&child.api,song_id)].visible && child.entries[Index(&child.api,song_id)].enabled);
  use_real_file_attributes=true;
  const auto opens_before_real_file=folder_opens;
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_opens==opens_before_real_file+1 && opened_folder==real_folder && folder_creations==0);
  assert(DeleteFileW(real_file.c_str()));
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_opens==opens_before_real_file+1 && last_message==missing_song_notice && folder_creations==0);
  assert(RemoveDirectoryW(real_folder.c_str()));
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,song_id,0);
  assert(folder_opens==opens_before_real_file+1 && GetFileAttributesW(real_folder.c_str())==INVALID_FILE_ATTRIBUTES && folder_creations==0);
  use_real_file_attributes=false;SetSongFile(actual.identity,L"");build_root();
  assert(child.entries[Index(&child.api,song_id)].visible && !child.entries[Index(&child.api,song_id)].enabled);
  q.format=history::PlaybackFormat::Ogg;q.level=history::PlaybackLevel::VeryHigh;q.bits=0;
  actual.level=q.level;history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[quality_index+2].visible && child.entries[quality_index+3].visible);
  q.rate=0;history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[quality_index+2].visible && !child.entries[quality_index+3].visible);
  q.rate=44100;
  for(auto format:{history::PlaybackFormat::Mp3,history::PlaybackFormat::Aac}) {
    q.format=format;q.level=history::PlaybackLevel::Lossy;q.decoder_bound=true;
    actual.level=history::PlaybackLevel::Lossless;
    history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
    build_root();assert(child.entries[quality_index+2].visible&&child.entries[quality_index+3].visible);
    assert(history::ReadPlaybackQuality().level==history::PlaybackLevel::Lossy);
  }
  q.decoder_bound=false;
  q.format=history::PlaybackFormat::Flac;q.level=history::PlaybackLevel::Lossless;q.bits=16;
  actual.level=q.level;history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[quality_index+2].visible && child.entries[quality_index+3].visible);
  q.format=history::PlaybackFormat::Unknown;history::PublishPlaybackQuality(q);
  build_root();assert(!child.entries[quality_index+2].visible && !child.entries[quality_index+3].visible);
  assert(child.entries[quality_index].visible && child.entries[quality_index+1].visible);
  assert(child.entries.size()==(never_pause_hidden?13u:14u) && child.refs==1 && root.refs==1);
  actual.level=history::PlaybackLevel::Unknown;q.level=actual.level;
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(!child.entries[quality_index+1].visible); // No Quality: Unavailable placeholder.
  actual.level=history::PlaybackLevel::Lossless;q.level=actual.level;
  q.format=history::PlaybackFormat::Flac;q.rate=0;
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[quality_index+1].visible && child.entries[quality_index+2].visible && !child.entries[quality_index+3].visible);
  history::client_updated=GetTickCount64()-4000;
  assert(history::ReadPlaybackQuality().identity==0); // Never regress to stale SMTC.
  history::PublishClientPlaybackQuality({});
  assert(history::ReadPlaybackQuality().title[0]==0); // No active cached song.
  for (size_t i = quality_index; i < quality_index+4; i++) {
    Retain(&child.api.base);
    Execute(&wrapped.api, &child.api, child.entries[i].id, 0);
  }
  assert(original_commands == 0 && child.refs == 1);
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,podcasts_id,0);
  assert(history::GetSettings().podcasts);
  assert(formats.entries[Index(&formats.api,podcasts_id)].checked);
  assert(formats.entries[Index(&formats.api,podcasts_id)].label==L"Podcasts");
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,podcasts_id,0);
  assert(!history::GetSettings().podcasts);
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,lyrics_id,0);
  assert(!history::GetSettings().lyrics);
  assert(!formats.entries[Index(&formats.api,lyrics_id)].checked);
  assert(formats.entries[Index(&formats.api,lyrics_id)].label==L"Include Lyrics");
  Retain(&formats.api.base);Execute(&wrapped.api,&formats.api,lyrics_id,0);
  assert(history::GetSettings().lyrics && history::FlushSettings(5000));
  const auto never_pause_epoch=history::CaptureEpoch();
  if(never_pause_hidden) {
    assert(Index(&child.api,28471)<0);
    Retain(&child.api.base);Execute(&wrapped.api,&child.api,28471,0);
    assert(history::GetSettings().never_pause==-1);
  } else {
    const auto row=Index(&child.api,28471);assert(row>=0&&!child.entries[row].checked);
    Retain(&child.api.base);Execute(&wrapped.api,&child.api,28471,0);
    assert(history::GetSettings().never_pause==1&&child.entries[row].checked);
    assert(child.entries[row].label==L"Never Pause");
    Retain(&child.api.base);Execute(&wrapped.api,&child.api,28471,0);
    assert(history::GetSettings().never_pause==0&&!child.entries[row].checked);
    assert(child.entries[row].label==L"Never Pause");
  }
  assert(history::CaptureEpoch()==never_pause_epoch&&history::FlushSettings(5000));
  const auto auto_skip_epoch=history::CaptureEpoch();
  Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,auto_skip_id,0);
  assert(history::GetSettings().auto_skip_downloaded&&history::CaptureEpoch()==auto_skip_epoch);
  assert(misc.entries[Index(&misc.api,auto_skip_id)].checked);
  assert(misc.entries[Index(&misc.api,auto_skip_id)].label==L"Auto Skip Downloaded");
  Retain(&misc.api.base);Execute(&wrapped.api,&misc.api,auto_skip_id,0);
  assert(!history::GetSettings().auto_skip_downloaded&&history::FlushSettings(5000));
  Retain(&child.api.base);
  Closed(&wrapped.api,&child.api);
  assert(child.refs==1 && !wrapped.capability.validated);
  capability_queries=0;
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);Format(&wrapped.api,&child.api,&rendered);
  if(rendered.dtor)rendered.dtor(rendered.str);
  assert(capability_queries>0 && child.refs==1); // Revalidate after closing.
  // Same address with a changed capability table must not hit the cache.
  auto old_method=child.api.methods[0];child.api.methods[0]=nullptr;
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);
  assert(Format(&wrapped.api,&child.api,&rendered)==0);
  assert(rendered.str==old_label.data() && child.refs==1);
  child.api.methods[0]=old_method;
  wrapped.ClearValidation();assert(child.refs==1);
  // A previously installed submenu trampoline remains owned on startup failure.
  menu_init.Retry(0); // Start a separate simulated failure case.
  assert(original_add_submenu&&menu_init.TryBegin(GetTickCount64()+1000));menu_prepared=true;
  FinalizeToDiskMenu(false);assert(menu_init.State()==hooks::InitState::Unsupported);
  assert(child.child->refs==1 && child.refs==1 && root.refs==1);
  std::puts("PASS: disabled first-row Floggfy heading, separator, duplicate-free population and no-op command ownership");
  std::puts("PASS: bounded recent labels escape mnemonics, sanitize controls, preserve Unicode pairs and full stored titles");
  std::puts("PASS: session downloads hidden until populated; stable labels and shell targets, ten-item cap, busy snapshot retention, missing-file notice");
  std::puts("PASS: menu preparation can retry; failed activation preserves trampoline state");
  std::puts("PASS: Song requires saved audio, rechecks deleted files/folders on click, never creates folders and preserves frozen targets");
  std::puts("PASS: bottom technical quality rows, independent codec display, duplicate-free "
            "refresh and disabled-command ownership");
  return 0;
}
