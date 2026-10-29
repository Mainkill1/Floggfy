#define WIN32_LEAN_AND_MEAN
#include "to_disk_menu.h"
#include "cef_menu_capability.h"
#include "cef_menu_abi.h"
#include "hook_init_state.h"
#include "hook_startup_minhook.h"
#include "history_settings.h"
#include "async_log.h"
#include "hook_diagnostics.h"
#include "playback_quality.h"
#include "recent_downloads.h"
#include "song_destination.h"
#include <shellapi.h>
#include <shlobj.h>
#include <memory>
#include "vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>

namespace {
// Public CEF C API prefix. CEF appends methods and publishes each structure's
// byte size, so runtime capability checks can accept newer compatible builds.
using Base=cef_menu::abi::Base;
using String=cef_menu::abi::String;
using Menu=cef_menu::abi::Menu;
using MenuMethods=cef_menu::abi::Methods;
using MenuReference=cef_menu::abi::Reference;
struct Delegate {
    Base base;
    void(*execute)(Delegate*,Menu*,int,int);
    void(*outside)(Delegate*,Menu*,const void*);
    void(*open)(Delegate*,Menu*,int);
    void(*close)(Delegate*,Menu*,int);
    void(*will_show)(Delegate*,Menu*);
    void(*closed)(Delegate*,Menu*);
    int(*format)(Delegate*,Menu*,String*);
};
static_assert(sizeof(Base)==cef_menu::kBaseBytes &&
              sizeof(Menu)==cef_menu::kRequiredMenuBytes &&
              sizeof(Delegate)==cef_menu::kKnownDelegateBytes,
              "CEF public-prefix ABI");
using Create=Menu*(*)(Delegate*);
Create original_create;
using SetString=int(*)(const wchar_t*,size_t,String*,int);
SetString set_string=nullptr;
using AddSubmenu=MenuMethods::AddSubmenuFunction;
AddSubmenu original_add_submenu;
hooks::InitController menu_init;
hooks::CallbackCounter menu_callbacks;
hooks::QueuedHookSet<1> menu_startup_hooks;
bool menu_prepared=false;
hooks::InitController submenu_init;
constexpr int root_id=28480,downloads_id=28481,location_id=28482,flac_id=28483,ogg_id=28484;
constexpr int song_id=28485,last_quality_id=song_id+3,lyrics_id=28489;
constexpr int recent_id=28490,first_recent_id=28491;
static_assert(root_id>=cef_menu::kUserCommandFirst &&
              first_recent_id+history::kRecentDownloadLimit-1<=cef_menu::kUserCommandLast,
              "Recent commands stay within the CEF user command range (26500..28500)");
static String Text(const std::wstring& s) { return {const_cast<wchar_t*>(s.c_str()),s.size(),nullptr}; }
static bool Executable(const void* address) {
    if(!address) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(address,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
       (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD protection=memory.Protect&0xff;
    return protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
           protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
static bool Capable(Menu* menu) {
    if(!menu || menu->base.size<cef_menu::kBaseBytes ||
       !Executable(reinterpret_cast<const void*>(menu->base.add_ref)) ||
       !Executable(reinterpret_cast<const void*>(menu->base.release))) return false;
    const size_t method_count=(menu->base.size-cef_menu::kBaseBytes)/sizeof(void*);
    if(!cef_menu::SupportsMenu(menu->base.size,menu->methods,method_count)) return false;
    for(const auto slot:cef_menu::kRequiredMenuSlots)
        if(!Executable(menu->methods[slot])) return false;
    return true;
}
struct MenuCapabilities {
    // Cache executable capability by the copied function table, never model
    // address: CEF can destroy/reuse model wrappers between callbacks. This
    // owns no model reference, so the model/delegate cannot form a cycle.
    std::array<const void*,cef_menu::kRequiredMenuSlots.size()+2> functions{};
    bool validated=false;
    size_t validated_size=0;
    void ClearValidation() { validated=false; }
    bool Check(Menu* menu) {
        if(!menu || menu->base.size<cef_menu::kRequiredMenuBytes) return false;
        decltype(functions) current{};
        current[0]=reinterpret_cast<const void*>(menu->base.add_ref);
        current[1]=reinterpret_cast<const void*>(menu->base.release);
        for(size_t i=0;i<cef_menu::kRequiredMenuSlots.size();++i)
            current[i+2]=menu->methods[cef_menu::kRequiredMenuSlots[i]];
        if(validated && menu->base.size==validated_size && current==functions) return true;
        ClearValidation();
        if(!Capable(menu)) return false;
        functions=current;validated_size=menu->base.size;validated=true;return true;
    }
};
static bool Main(Menu* menu) {
    if(MenuMethods::IsSubmenu(menu)) return false;
    const size_t count=MenuMethods::Count(menu); if(count<3 || count>30) return false;
    int types[30]{};
    for(size_t index=0;index<count;++index) types[index]=MenuMethods::TypeAt(menu,index);
    return cef_menu::LooksLikeMainMenu(false,types,count);
}
static void UpdateQuality(Menu* menu,const history::PlaybackQualitySnapshot* song=nullptr,const std::wstring* folder=nullptr) {
    const auto snapshot=history::ReadPlaybackQuality();
    auto labels=history::PlaybackQualityLabels(snapshot);
    if(song) labels[0]=history::PlaybackQualityLabels(*song)[0];
    const bool flac=snapshot.format==history::PlaybackFormat::Flac;
    if(MenuMethods::IndexOf(menu,song_id)<0) MenuMethods::AddSeparator(menu);
    for(unsigned i=0;i<labels.size();++i) {
        int id=song_id+int(i);auto text=Text(labels[i]);
        if(MenuMethods::IndexOf(menu,id)<0)MenuMethods::AddItem(menu,id,&text);
        else MenuMethods::SetLabel(menu,id,&text);
        MenuMethods::SetEnabled(menu,id,int(i==0 && folder && !folder->empty()));
        const bool visible=i==0 || (i==1 ? snapshot.level!=history::PlaybackLevel::Unknown :
                                     flac && (i==2 || snapshot.rate!=0));
        MenuMethods::SetVisible(menu,id,int(visible));
    }
}
static void Populate(Menu* menu) {
    static thread_local MenuCapabilities capability;
    if(!capability.Check(menu) || MenuMethods::IsSubmenu(menu)) return;
    // Construction invokes us once for every submenu. Once our root exists,
    // do no structural scan, snapshot copy, child refresh or insertion work.
    if(MenuMethods::IndexOf(menu,root_id)>=0) return;
    if(!Main(menu)) {
        static volatile LONG logged=0;
        if(InterlockedCompareExchange(&logged,1,0)==0)
            history::LogEvent(history::LogKind::Warning,"warning","To Disk menu capability: top-level submenu structure not recognized");
        return;
    }
    auto state=history::GetSettings();
    Menu* child=MenuMethods::GetSubmenu(menu,root_id);
    bool inserted=false;
    if(!child) {
        if(MenuMethods::IndexOf(menu,root_id)>=0) return; // conflicting command id
        std::wstring label=L"To Disk"; auto text=Text(label);
        child=MenuMethods::AddSubmenu(menu,root_id,&text); inserted=true;
    }
    if(!Capable(child)) { if(child && child->base.release) child->base.release(&child->base); return; }
    MenuReference child_reference(child);
    if(inserted) MenuMethods::Clear(child);
    auto item=[child](int id,const std::wstring& label,bool checked,bool enabled) {
        auto text=Text(label);
        if(MenuMethods::IndexOf(child,id)<0) MenuMethods::AddCheckItem(child,id,&text);
        else MenuMethods::SetLabel(child,id,&text);
        MenuMethods::SetChecked(child,id,int(checked)); MenuMethods::SetEnabled(child,id,int(enabled));
    };
    item(downloads_id,state.downloads ? L"Downloads (Enabled)" : L"Downloads (Disabled)",state.downloads,true);
    if(inserted) { std::wstring location=L"Save Location"; auto text=Text(location); MenuMethods::AddItem(child,location_id,&text); }
    if(inserted) {
        std::wstring label=L"Latest Downloads"; auto text=Text(label);
        if(auto* recent=MenuMethods::AddSubmenu(child,recent_id,&text)) {
            MenuReference recent_reference(recent);
            std::wstring empty=L"No downloads yet"; auto placeholder=Text(empty);
            MenuMethods::AddItem(recent,first_recent_id,&placeholder);
            MenuMethods::SetEnabled(recent,first_recent_id,0);
        }
        MenuMethods::SetEnabled(child,recent_id,0);
        MenuMethods::SetVisible(child,recent_id,0);
    }
    item(flac_id,state.flac ? L"FLAC (Enabled)" : L"FLAC (Disabled)",state.flac,true);
    item(ogg_id,state.ogg ? L"Ogg (Enabled)" : L"Ogg (Disabled)",state.ogg,true);
    item(lyrics_id,state.lyrics ? L"Lyrics (Enabled)" : L"Lyrics (Disabled)",state.lyrics,true);
    UpdateQuality(child);
}
static Menu* AddSubmenuHook(Menu* menu,int id,const String* text) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuSubmenu);
    auto callback=menu_callbacks.Enter();
    Menu* result=original_add_submenu(menu,id,text);
    // MenuWillShow occurs after CEF has built the visible menu. Insert while
    // Spotify constructs the model so the first opening contains our items.
    static thread_local bool populating=false;
    if(!populating) {
        struct Reset {bool& value;~Reset(){value=false;}} reset{populating};
        populating=true;
        try {Populate(menu);}catch(...) {/* Preserve the client model on allocation failure. */}
    }
    return result;
}
struct MenuOpening {
    enum class Phase {Preparing,Visible};
    struct Snapshot {
        history::PlaybackQualitySnapshot song{};
        std::wstring song_file;
        std::vector<history::RecentDownload> recent;
        bool song_bound=false;
        unsigned song_settings_generation=0;
    } snapshot;
    Phase phase=Phase::Preparing;
    bool Visible()const{return phase==Phase::Visible;}
    void Show(){phase=Phase::Visible;}
    void Close(){phase=Phase::Preparing;snapshot.song_bound=false;}
};
struct Wrapped {
    Delegate api;
    std::atomic<unsigned> refs{1};
    Delegate* original=nullptr;
    MenuCapabilities capability;
    // The shared delegate owns command targets, never a retained menu model.
    MenuOpening opening;
    bool CapableCached(Menu* menu) { return capability.Check(menu); }
    void ClearValidation() { capability.ClearValidation(); }
    ~Wrapped() { ClearValidation(); if(original) original->base.release(&original->base); }
};
static Wrapped* Self(void* p) { return static_cast<Wrapped*>(p); }
static void Add(Base* p) { Self(p)->refs.fetch_add(1,std::memory_order_relaxed); }
static int Release(Base* p) { auto* w=Self(p); if(w->refs.fetch_sub(1,std::memory_order_acq_rel)==1) { delete w; return 1; } return 0; }
static int One(Base* p) { return Self(p)->refs.load()==1; }
static int Any(Base* p) { return Self(p)->refs.load()!=0; }
static void ReleaseMenu(Menu* menu) { if(menu && menu->base.release) menu->base.release(&menu->base); }
static void RefreshPendingSongTarget(Wrapped* wrapped) {
    auto& opening=wrapped->opening.snapshot;
    if(!opening.song_bound || !opening.song_file.empty())return;
    std::wstring saved_file;
    if(history::ReadSongDestination(opening.song.identity,opening.song_settings_generation,saved_file))
        opening.song_file.swap(saved_file);
}
static void BindSong(Wrapped* wrapped) {
    auto& opening=wrapped->opening.snapshot;
    opening.song=history::ReadPlaybackQuality();
    opening.song_settings_generation=history::GetSettings().generation;
    opening.song_file.clear();opening.song_bound=true;
    RefreshPendingSongTarget(wrapped);
}
static std::wstring RecentLabel(const std::wstring& title) {
    if(title.empty()) return L"Untitled download";
    // Bound displayed UTF-16 before escaping CEF mnemonic markers. Match the
    // Song row's 96-unit limit without leaving half a surrogate pair behind.
    const bool truncated=title.size()>96;
    std::wstring bounded=title.substr(0,truncated ? 95 : title.size());
    if(truncated) {
        if(bounded.back()>=0xd800 && bounded.back()<=0xdbff) bounded.pop_back();
        bounded+=L'\u2026';
    }
    std::wstring label;
    label.reserve(bounded.size()*2);
    for(wchar_t c:bounded) {
        if(c<L' ' || (c>=0x7f && c<=0x9f)) c=L' ';
        label+=c;
        if(c==L'&') label+=c;
    }
    return label;
}
static void RefreshRecent(Wrapped* wrapped,Menu* child) {
    std::vector<history::RecentDownload> next;
    if(!history::ReadRecentDownloads(next)) return; // Never block or erase a prior snapshot.
    if(next.size()>history::kRecentDownloadLimit) next.resize(history::kRecentDownloadLimit);
    // Complete allocations before retaining or changing CEF models. The stored
    // snapshot keeps full titles and paths; only labels receive display hygiene.
    std::vector<std::wstring> labels;
    labels.reserve(next.size());
    for(const auto& entry:next) labels.push_back(RecentLabel(entry.title));
    auto* recent=MenuMethods::GetSubmenu(child,recent_id);
    if(!recent)return;
    MenuReference recent_reference(recent);
    if(!wrapped->CapableCached(recent) || !MenuMethods::Clear(recent))return;
    wrapped->opening.snapshot.recent.swap(next);
    if(wrapped->opening.snapshot.recent.empty()) {
        wchar_t empty[]=L"No downloads yet";String text{empty,15,nullptr};
        MenuMethods::AddItem(recent,first_recent_id,&text);
        MenuMethods::SetEnabled(recent,first_recent_id,0);
    } else for(size_t i=0;i<wrapped->opening.snapshot.recent.size();++i) {
        auto text=Text(labels[i]);MenuMethods::AddItem(recent,first_recent_id+int(i),&text);
    }
    MenuMethods::SetEnabled(child,recent_id,!wrapped->opening.snapshot.recent.empty());
    MenuMethods::SetVisible(child,recent_id,!wrapped->opening.snapshot.recent.empty());
}
static DWORD WINAPI RevealRecentWorker(void* argument) {
    std::unique_ptr<std::wstring> path(static_cast<std::wstring*>(argument));
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const auto attributes=GetFileAttributesW(history::WindowsPath(*path).c_str());
    HRESULT result=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        PIDLIST_ABSOLUTE item=nullptr;
        result=SHParseDisplayName(path->c_str(),nullptr,&item,0,nullptr);
        if(SUCCEEDED(result)) result=SHOpenFolderAndSelectItems(item,0,nullptr,0);
        CoTaskMemFree(item);
    }
    if(FAILED(result)) MessageBoxW(nullptr,
        attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY) ?
        L"This download is no longer available at its saved location." :
        L"Explorer could not show this download.",L"Latest Downloads",MB_OK|MB_ICONINFORMATION);
    if(SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
static void RevealRecent(const std::wstring& path) {
    auto* copy=new(std::nothrow) std::wstring(path);
    if(!copy) return;
    // Filesystem and shell work belongs to a click, away from CEF's UI thread.
    HANDLE worker=CreateThread(nullptr,0,RevealRecentWorker,copy,0,nullptr);
    if(worker) CloseHandle(worker);
    else {delete copy;MessageBoxW(GetActiveWindow(),L"Explorer could not be started.",L"Latest Downloads",MB_OK|MB_ICONINFORMATION);}
}
static DWORD WINAPI RevealSongWorker(void* argument) {
    std::unique_ptr<std::wstring> saved_file(static_cast<std::wstring*>(argument));
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool available=false,opened=false;
    try {
        // A file may disappear after the worker snapshot was displayed. Check
        // the bound file again on this click, without recreating its folder.
        const auto attributes=GetFileAttributesW(history::WindowsPath(*saved_file).c_str());
        available=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
        const auto slash=saved_file->find_last_of(L"\\/");
        if(available && slash!=std::wstring::npos) {
            const auto folder=saved_file->substr(0,slash);
            opened=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
        }
    } catch(...) {/* Never unwind out of a click worker on allocation failure. */}
    if(!opened)
        MessageBoxW(nullptr,available ? L"Explorer could not open this song's folder." :
                    L"This song's saved audio is no longer available at its saved location.",
                    L"Song folder",MB_OK|MB_ICONINFORMATION);
    if(SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
static void RevealSong(const std::wstring& saved_file) {
    auto* copy=new(std::nothrow) std::wstring(saved_file);
    if(!copy) return;
    HANDLE worker=CreateThread(nullptr,0,RevealSongWorker,copy,0,nullptr);
    if(worker) CloseHandle(worker);
    else {delete copy;MessageBoxW(GetActiveWindow(),L"The song folder could not be opened.",L"Song folder",MB_OK|MB_ICONERROR);}
}
static void Execute(Delegate* self,Menu* menu,int command,int flags) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuExecute);
    MenuReference reference(menu);
    try {
    if(command==song_id) {
        try {if(!Self(self)->opening.snapshot.song_file.empty()) RevealSong(Self(self)->opening.snapshot.song_file);}
        catch(...) {/* Keep allocation failures inside the CEF callback. */}
        return;
    }
    if(command>=first_recent_id && command<first_recent_id+int(history::kRecentDownloadLimit)) {
        const auto index=size_t(command-first_recent_id);
        try {if(index<Self(self)->opening.snapshot.recent.size()) RevealRecent(Self(self)->opening.snapshot.recent[index].path);}
        catch(...) {/* CEF callbacks must not propagate allocation failures. */}
        return;
    }
    if(command==recent_id) {return;}
    if(command>=song_id && command<=last_quality_id) {return;}
    auto* w=Self(self); auto settings=history::GetSettings(); bool handled=true,ok=true;
    switch(command) {
        case downloads_id: ok=history::SetDownloads(!settings.downloads); break;
        case ogg_id: ok=history::SetOgg(!settings.ogg); break;
        case location_id: history::PickSaveLocation(GetActiveWindow()); break;
        case lyrics_id: ok=history::SetLyrics(!settings.lyrics); break;
        case flac_id: ok=history::SetFlac(!settings.flac); break;
        default: handled=false;
    }
    if(handled) {
        if(command==downloads_id || command==ogg_id || command==flac_id || command==lyrics_id) {
            auto updated=history::GetSettings();
            bool value=command==downloads_id ? updated.downloads : command==ogg_id ? updated.ogg : command==lyrics_id ? updated.lyrics : updated.flac;
            std::wstring label=command==downloads_id ? L"Downloads" : command==ogg_id ? L"Ogg" : command==lyrics_id ? L"Lyrics" : L"FLAC";
            label+=value ? L" (Enabled)" : L" (Disabled)"; auto text=Text(label);
            MenuMethods::SetLabel(menu,command,&text); MenuMethods::SetChecked(menu,command,int(value));
            char line[160]; snprintf(line,sizeof(line),"To Disk command=%d accepted=%d enabled=%d",command,ok,value); history::LogEvent(ok?history::LogKind::Activity:history::LogKind::Failure,ok?"settings":"failed",line);
        }
        if(!ok) MessageBoxW(GetActiveWindow(),L"The setting could not be saved.",L"To Disk",MB_OK|MB_ICONERROR);
        return;
    }
    if(w->original && w->original->execute) w->original->execute(w->original,reference.Take(),command,flags);
    }catch(...) {/* Do not unwind allocating settings/UI operations through CEF. */}
}
static void Outside(Delegate* self,Menu* menu,const void* point) { auto* o=Self(self)->original; if(o && o->outside) o->outside(o,menu,point); else ReleaseMenu(menu); }
static void Open(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->open) o->open(o,menu,rtl); else ReleaseMenu(menu); }
static void Close(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->close) o->close(o,menu,rtl); else ReleaseMenu(menu); }
static void WillShow(Delegate* self,Menu* menu) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuWillShow);
    MenuReference reference(menu);
    try {
    if(Self(self)->CapableCached(menu) && MenuMethods::IndexOf(menu,root_id)>=0) Self(self)->opening.Show();
    auto* o=Self(self)->original;
    // CEF transfers one menu reference into every delegate callback. Retain
    // another across the original callback, which consumes its argument.
    if(o && o->will_show) { menu->base.add_ref(&menu->base); o->will_show(o,menu); }
    if(Self(self)->CapableCached(menu) && MenuMethods::IndexOf(menu,song_id)>=0) {
        if(!Self(self)->opening.snapshot.song_bound) BindSong(Self(self));
        else RefreshPendingSongTarget(Self(self));
        UpdateQuality(menu,&Self(self)->opening.snapshot.song,&Self(self)->opening.snapshot.song_file);
    }
    else Populate(menu);
    }catch(...) {/* Keep prior labels if allocation fails. */}
}
static void Closed(Delegate* self,Menu* menu) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuClosed);
    MenuReference reference(menu);
    if(Self(self)->CapableCached(menu) && MenuMethods::IndexOf(menu,root_id)>=0) Self(self)->opening.Close();
    Self(self)->ClearValidation();
    auto* original=Self(self)->original;
    if(original && original->closed)original->closed(original,reference.Take());
}
static int Format(Delegate* self,Menu* menu,String* label) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuFormat);
    MenuReference reference(menu);
    auto* o=Self(self)->original;int changed=0;
    try {
    if(o && o->format) {
        menu->base.add_ref(&menu->base);
        changed=o->format(o,menu,label);
    }
    // Refresh visibility while CEF formats the parent label, before it builds
    // the submenu's visible children. WillShow alone is too late for the first
    // opening after a codec change on a reused model.
    if(label && label->str && label->length==7 && !wmemcmp(label->str,L"To Disk",7) &&
       Self(self)->CapableCached(menu) && MenuMethods::IndexOf(menu,root_id)>=0) {
        if(auto* child=MenuMethods::GetSubmenu(menu,root_id)) {
            MenuReference child_reference(child);
            try { if(Self(self)->CapableCached(child) && MenuMethods::IndexOf(child,song_id)>=0 &&
                     MenuMethods::IndexOf(child,downloads_id)>=0) {
                         if(!Self(self)->opening.Visible()) BindSong(Self(self));
                         else RefreshPendingSongTarget(Self(self));
                         UpdateQuality(child,&Self(self)->opening.snapshot.song,&Self(self)->opening.snapshot.song_file);
                         if(!Self(self)->opening.Visible()) RefreshRecent(Self(self),child);
                     } }
            catch(...) {/* Keep prior visibility on allocation failure. */}
        }
    }
    // CEF constructs visible labels before MenuWillShow. Its public label
    // formatter supplies fresh text during construction, on the UI thread.
    // Reject ordinary labels before capability queries or model scans. Most
    // menus contain no quality rows, so this keeps their formatting cheap.
    if(set_string && label && label->str) {
        const wchar_t* prefixes[]={L"Song: ",L"Quality: ",L"Format: ",L"Sample rate: "};
        for(unsigned i=0;i<4;++i) {
            const auto length=wcslen(prefixes[i]);
            if(label->length<length || wmemcmp(label->str,prefixes[i],length)) continue;
            if(!Self(self)->CapableCached(menu) || MenuMethods::IndexOf(menu,song_id)<0 ||
               MenuMethods::IndexOf(menu,downloads_id)<0) break;
            try {
                if(i==0 && !Self(self)->opening.snapshot.song_bound) BindSong(Self(self));
                else if(i==0)RefreshPendingSongTarget(Self(self));
                const auto rows=history::PlaybackQualityLabels(i==0 ? Self(self)->opening.snapshot.song : history::ReadPlaybackQuality());
                if(i==0) MenuMethods::SetEnabled(menu,song_id,int(!Self(self)->opening.snapshot.song_file.empty()));
                if(set_string(rows[i].data(),rows[i].size(),label,1))changed=1;
            }catch(...) {/* Keep the original CEF label on allocation failure. */}
            break;
        }
    }
    }catch(...) {/* Keep the last complete opening on allocation failure. */}
    return changed;
}
static Menu* Hook(Delegate* delegate) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuCreate);
    auto callback=menu_callbacks.Enter();
    if(!delegate || !cef_menu::SupportsDelegate(delegate->base.size) ||
       !Executable(reinterpret_cast<const void*>(delegate->base.add_ref)) ||
       !Executable(reinterpret_cast<const void*>(delegate->base.release)))
        return original_create(delegate);
    auto* w=new(std::nothrow) Wrapped;
    if(!w) return original_create(delegate);
    w->api={{sizeof(Delegate),Add,Release,One,Any},Execute,Outside,Open,Close,WillShow,Closed,Format};
    // The incoming delegate and the replacement each carry a transferred
    // reference. CEF Wrap() consumes it; releasing again here frees a live
    // callback object. Hold the original transferred reference until teardown.
    w->original=delegate;
    Menu* result=original_create(&w->api);
    if(Capable(result) && submenu_init.TryBegin(GetTickCount64())) {
        void* address=reinterpret_cast<void*>(MenuMethods::SubmenuImplementation(result));
        MH_STATUS status=address ? MH_CreateHook(address,reinterpret_cast<void*>(AddSubmenuHook),reinterpret_cast<void**>(&original_add_submenu)) : MH_ERROR_NOT_EXECUTABLE;
        bool created=status==MH_OK;
        if(status==MH_OK) status=MH_EnableHook(address);
        if(status==MH_OK) submenu_init.Activate();
        else {
            if(created) MH_RemoveHook(address);
            submenu_init.Retry(GetTickCount64());
        }
        if(status!=MH_OK)history::LogEvent(history::LogKind::Warning,"warning","To Disk menu construction hook unavailable; retry scheduled");
    }
    return result;
}
}
static void FinishMenuStartup(bool applied,bool activation_attempted) {
    if(!menu_prepared)return;
    menu_prepared=false;
    if(applied){menu_init.Activate();return;}
    const auto rollback=hooks::RollbackStartupHooks(menu_startup_hooks,[]{
        for(unsigned waited=0;menu_callbacks.Active()&&waited<5000;++waited)Sleep(1);
        return menu_callbacks.Active()==0;
    },!activation_attempted);
    // A thread can be entering a detour before its callback counter increments.
    // After any activation attempt retain all trampolines, even with no submenu.
    // A partially applied root may already have installed the submenu hook.
    // Its callback/delegate state must survive even if root cleanup succeeded.
    if(!activation_attempted&&rollback.CanRelease()&&!original_add_submenu){
        menu_init.Retry(GetTickCount64());history::LogEvent(history::LogKind::Warning,"warning","To Disk startup activation failed; rollback complete; retry scheduled");
    } else {
        menu_init.MarkUnsupported();history::LogEvent(history::LogKind::Warning,"warning","To Disk startup activation failed or rollback unsafe; trampoline state preserved and retries disabled");
    }
}
void FinalizeToDiskMenu(bool applied) {FinishMenuStartup(applied,true);}
bool PrepareToDiskMenu(HMODULE cef) {
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!menu_init.TryBegin(now)) return false;
    if(!history::GetSettings().menu) { history::QueueDiagnostic("To Disk menu disabled by INI Menu=0"); menu_init.MarkUnsupported(); return false; }
    auto string_set=GetProcAddress(cef,"cef_string_utf16_set");
    memcpy(&set_string,&string_set,sizeof(set_string));
    if(!set_string)history::LogEvent(history::LogKind::Warning,"warning","To Disk live labels unavailable: cef_string_utf16_set missing");
    auto create=GetProcAddress(cef,"cef_menu_model_create");
    if(!create || !Executable(reinterpret_cast<const void*>(create))) {
        history::LogEvent(history::LogKind::Warning,"warning","To Disk menu unavailable: cef_menu_model_create capability missing"); menu_init.MarkUnsupported(); return false;
    }
    menu_prepared=true;
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=MH_CreateHook(reinterpret_cast<void*>(create),reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original_create));
    if(status==MH_OK)menu_startup_hooks.Add(reinterpret_cast<void*>(create));
    if(status!=MH_OK||!hooks::QueueStartupHooks(menu_startup_hooks)){
        char line[180];snprintf(line,sizeof(line),"To Disk startup hook preparation failed: %s",MH_StatusToString(status));history::QueueDiagnostic(line);
        FinishMenuStartup(false,false);return false;
    }
    return true;
}
void StartToDiskMenu(HMODULE cef) {
    if(PrepareToDiskMenu(cef))FinalizeToDiskMenu(MH_ApplyQueued()==MH_OK);
}
