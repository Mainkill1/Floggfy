// Exercise the production CEF menu boundary with a model implementing the
// required public prefix. No Spotify binaries or private media are needed.
#include "../native/playback_quality_windows.cpp"
#include "../native/to_disk_menu.cpp"
#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>
struct Entry {
  int id = 0, type = 1;
  std::wstring label;
  bool enabled = true, checked = false;
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
static int Index(Menu *m, int id) {
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
  assert(index < M(m).entries.size());
  return M(m).entries[index].type;
}
static int Enable(Menu *m, int id, int value) {
  int i = Index(m, id);
  assert(i >= 0);
  M(m).entries[size_t(i)].enabled = value;
  return 1;
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
int main() {
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
  assert(child.entries.size() == 9); // Four controls, separator, four quality rows.
  for (size_t i = 5; i < 9; i++)
    assert(child.entries[i].type == 1 && !child.entries[i].enabled && !child.entries[i].checked);
  assert(child.entries[5].label.find(L"Example") != std::wstring::npos);
  assert(child.entries[7].label.find(L"Ogg") != std::wstring::npos);
  Populate(&root.api);
  assert(child.entries.size() == 9 && child.refs == 1);
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
    assert(translated.child->entries.size()==9 && translated.child->refs==1);
    Populate(&translated.api);
    assert(translated.entries.size()==4 && translated.child->entries.size()==9);
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
  std::wstring untouched=L"Save Location";rendered=Text(untouched);
  Retain(&child.api.base);
  assert(Format(&wrapped.api,&child.api,&rendered)==0);
  assert(rendered.str==untouched.data() && format_calls==2 && child.refs==1);
  auto setter=set_string;set_string=nullptr;
  old_label=L"Song: Unavailable";rendered=Text(old_label);
  Retain(&child.api.base);
  assert(Format(&wrapped.api,&child.api,&rendered)==0);
  assert(rendered.str==old_label.data() && child.refs==1);
  set_string=setter;
  Retain(&child.api.base);
  WillShow(&wrapped.api, &child.api);
  assert(child.entries[7].label.find(L"FLAC") != std::wstring::npos &&
         child.entries[8].label.find(L"96,000 Hz") != std::wstring::npos);
  assert(child.entries[5].label==L"Song: Next");
  history::client_updated=GetTickCount64()-4000;
  assert(history::ReadPlaybackQuality().identity==0); // Never regress to stale SMTC.
  history::PublishClientPlaybackQuality({});
  assert(history::ReadPlaybackQuality().title[0]==0); // No active cached song.
  for (size_t i = 5; i < 9; i++) {
    Retain(&child.api.base);
    Execute(&wrapped.api, &child.api, child.entries[i].id, 0);
  }
  assert(original_commands == 0 && child.refs == 1);
  std::puts("PASS: bottom read-only quality rows, independent codec display, duplicate-free "
            "refresh and disabled-command ownership");
}
