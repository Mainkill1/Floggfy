// Exercise the production CEF menu boundary with a model implementing the
// required public prefix. No Spotify binaries or private media are needed.
#include "../native/playback_quality_windows.cpp"
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
#include "../native/to_disk_menu.cpp"
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
  if (Index(m, id) < 0 || !M(m).child)
    return nullptr;
  Retain(&M(m).child->api.base);
  return &M(m).child->api;
}
static void Init(Model &model);
static Menu *AddChild(Menu *m, int id, const String *s) {
  Item(m, id, s);
  M(m).entries.back().type = 5;
  M(m).child = std::make_unique<Model>();
  Init(*M(m).child);
  M(m).child->submenu = true;
  Retain(&M(m).child->api.base);
  if(id==recent_id&&fail_recent_placeholder){fail_recent_placeholder=false;fail_menu_allocation=true;}
  return &M(m).child->api;
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
#else
int main() {
#endif
  assert(menu_init.TryBegin(GetTickCount64()));menu_prepared=true;
  FinishMenuStartup(false,false);
  assert(menu_init.State()==hooks::InitState::RetryableFailure);
  // A failed application can race a detour entry before its callback counter.
  menu_init.Retry(0);assert(menu_init.TryBegin(GetTickCount64()));menu_prepared=true;
  FinalizeToDiskMenu(false);
  assert(menu_init.State()==hooks::InitState::Unsupported);
  history::InitSettings(nullptr);
  assert(history::SetLyrics(true));
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
  q.bitrate = 320000;
  q.level = history::PlaybackLevel::VeryHigh;
  history::PublishPlaybackQuality(q);
  Populate(&root.api);
  assert(root.child);
  auto &child = *root.child;
  assert(Index(&child.api,28490)==Index(&child.api,location_id)+1);
  assert(child.entries[Index(&child.api,28490)].label==L"Latest Downloads");
  assert(!child.entries[Index(&child.api,recent_id)].visible);
  assert(child.child && child.child->refs==1);
  assert(!child.child->entries[0].enabled);
  assert(filesystem_calls==0 && shell_calls==0 && recent_reads==0);
  assert(child.entries[Index(&child.api,lyrics_id)].checked);
  assert(child.entries.size() == 11); // Six controls, separator, four quality rows.
  for (size_t i = 7; i < 11; i++)
    assert(child.entries[i].type == 1 && !child.entries[i].enabled && !child.entries[i].checked);
  assert(child.entries[7].label.find(L"Example") != std::wstring::npos);
  assert(child.entries[7].visible && child.entries[8].visible);
  assert(!child.entries[9].visible && !child.entries[10].visible); // Non-FLAC details stay hidden, even after Ogg verification.
  type_calls=0;
  Populate(&root.api);
  assert(type_calls==0); // Existing insertion is not rescanned or repopulated.
  assert(child.entries.size() == 11 && child.refs == 1);
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
    assert(translated.child->entries.size()==11 && translated.child->refs==1);
    Populate(&translated.api);
    assert(translated.entries.size()==4 && translated.child->entries.size()==11);
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
  assert(child.entries[9].label.find(L"FLAC") != std::wstring::npos &&
         child.entries[10].label.find(L"96,000 Hz") != std::wstring::npos);
  assert(child.entries[7].label==L"Song: Next");
  assert(child.entries[9].visible && child.entries[10].visible); // Confirmed FLAC details remain.
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
  assert(shell_calls==2 && message_calls==1 && original_commands==0 && recent.refs==1);
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
  build_root();assert(!child.entries[9].visible && !child.entries[10].visible);
  q.format=history::PlaybackFormat::Flac;q.level=history::PlaybackLevel::Lossless;q.bits=16;
  actual.level=q.level;history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[9].visible && child.entries[10].visible);
  q.format=history::PlaybackFormat::Unknown;history::PublishPlaybackQuality(q);
  build_root();assert(!child.entries[9].visible && !child.entries[10].visible);
  assert(child.entries[7].visible && child.entries[8].visible);
  assert(child.entries.size()==11 && child.refs==1 && root.refs==1);
  actual.level=history::PlaybackLevel::Unknown;q.level=actual.level;
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(!child.entries[8].visible); // No Quality: Unavailable placeholder.
  actual.level=history::PlaybackLevel::Lossless;q.level=actual.level;
  q.format=history::PlaybackFormat::Flac;q.rate=0;
  history::PublishClientPlaybackQuality(actual);history::PublishPlaybackQuality(q);
  build_root();assert(child.entries[8].visible && child.entries[9].visible && !child.entries[10].visible);
  history::client_updated=GetTickCount64()-4000;
  assert(history::ReadPlaybackQuality().identity==0); // Never regress to stale SMTC.
  history::PublishClientPlaybackQuality({});
  assert(history::ReadPlaybackQuality().title[0]==0); // No active cached song.
  for (size_t i = 7; i < 11; i++) {
    Retain(&child.api.base);
    Execute(&wrapped.api, &child.api, child.entries[i].id, 0);
  }
  assert(original_commands == 0 && child.refs == 1);
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,lyrics_id,0);
  assert(!history::GetSettings().lyrics);
  assert(!child.entries[Index(&child.api,lyrics_id)].checked);
  assert(child.entries[Index(&child.api,lyrics_id)].label==L"Lyrics (Disabled)");
  Retain(&child.api.base);Execute(&wrapped.api,&child.api,lyrics_id,0);
  assert(history::GetSettings().lyrics && history::FlushSettings(5000));
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
  std::puts("PASS: bounded recent labels escape mnemonics, sanitize controls, preserve Unicode pairs and full stored titles");
  std::puts("PASS: session downloads hidden until populated; stable labels and shell targets, ten-item cap, busy snapshot retention, missing-file notice");
  std::puts("PASS: menu preparation can retry; failed activation preserves trampoline state");
  std::puts("PASS: Song requires saved audio, rechecks deleted files/folders on click, never creates folders and preserves frozen targets");
  std::puts("PASS: bottom technical quality rows, independent codec display, duplicate-free "
            "refresh and disabled-command ownership");
  return 0;
}
