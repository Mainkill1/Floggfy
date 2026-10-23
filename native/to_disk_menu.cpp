#define WIN32_LEAN_AND_MEAN
#include "to_disk_menu.h"
#include "cef_menu_capability.h"
#include "hook_init_state.h"
#include "hook_startup_minhook.h"
#include "history_settings.h"
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
struct Base { size_t size; void(*add_ref)(Base*); int(*release)(Base*);
    int(*has_one_ref)(Base*); int(*has_at_least_one_ref)(Base*); };
struct String { wchar_t* str; size_t length; void(*dtor)(wchar_t*); };
struct Menu { Base base; void* methods[cef_menu::kHighestRequiredMenuSlot+1]; };
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
using AddSubmenu=Menu*(*)(Menu*,int,const String*);
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
template<class R,class...A> R Call(Menu* m,unsigned slot,A...a) {
    if(!m || m->base.size<cef_menu::kRequiredMenuBytes ||
       slot>cef_menu::kHighestRequiredMenuSlot || !m->methods[slot]) return R{};
    return reinterpret_cast<R(*)(Menu*,A...)>(m->methods[slot])(m,a...);
}
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
    if(Call<int>(menu,0)) return false;
    const size_t count=Call<size_t>(menu,2); if(count<3 || count>30) return false;
    int types[30]{};
    for(size_t index=0;index<count;++index) types[index]=Call<int>(menu,23,index);
    return cef_menu::LooksLikeMainMenu(false,types,count);
}
static void UpdateQuality(Menu* menu,const history::PlaybackQualitySnapshot* song=nullptr,const std::wstring* folder=nullptr) {
    const auto snapshot=history::ReadPlaybackQuality();
    auto labels=history::PlaybackQualityLabels(snapshot);
    if(song) labels[0]=history::PlaybackQualityLabels(*song)[0];
    const bool flac=snapshot.format==history::PlaybackFormat::Flac;
    if(Call<int>(menu,15,song_id)<0) Call<int>(menu,3);
    for(unsigned i=0;i<labels.size();++i) {
        int id=song_id+int(i);auto text=Text(labels[i]);
        if(Call<int>(menu,15,id)<0)Call<int>(menu,4,id,&text);
        else Call<int>(menu,20,id,&text);
        Call<int>(menu,36,id,int(i==0 && folder && !folder->empty()));
        const bool visible=i==0 || (i==1 ? snapshot.level!=history::PlaybackLevel::Unknown :
                                     flac && (i==2 || snapshot.rate!=0));
        Call<int>(menu,32,id,int(visible));
    }
}
static void Populate(Menu* menu) {
    static thread_local MenuCapabilities capability;
    if(!capability.Check(menu) || Call<int>(menu,0)) return;
    // Construction invokes us once for every submenu. Once our root exists,
    // do no structural scan, snapshot copy, child refresh or insertion work.
    if(Call<int>(menu,15,root_id)>=0) return;
    if(!Main(menu)) {
        static volatile LONG logged=0;
        if(InterlockedIncrement(&logged)<=3)
            history::HistoryLog("To Disk menu capability: top-level submenu structure not recognized");
        return;
    }
    auto state=history::GetSettings();
    Menu* child=Call<Menu*>(menu,28,root_id);
    bool inserted=false;
    if(!child) {
        if(Call<int>(menu,15,root_id)>=0) return; // conflicting command id
        std::wstring label=L"To Disk"; auto text=Text(label);
        child=Call<Menu*>(menu,7,root_id,&text); inserted=true;
    }
    if(!Capable(child)) { if(child && child->base.release) child->base.release(&child->base); return; }
    if(inserted) Call<int>(child,1);
    auto item=[child](int id,const std::wstring& label,bool checked,bool enabled) {
        auto text=Text(label);
        if(Call<int>(child,15,id)<0) Call<int>(child,5,id,&text);
        else Call<int>(child,20,id,&text);
        Call<int>(child,40,id,int(checked)); Call<int>(child,36,id,int(enabled));
    };
    item(downloads_id,state.downloads ? L"Downloads (Enabled)" : L"Downloads (Disabled)",state.downloads,true);
    if(inserted) { std::wstring location=L"Save Location"; auto text=Text(location); Call<int>(child,4,location_id,&text); }
    if(inserted) {
        std::wstring label=L"Latest Downloads >"; auto text=Text(label);
        if(auto* recent=Call<Menu*>(child,7,recent_id,&text)) {
            std::wstring empty=L"No downloads yet"; auto placeholder=Text(empty);
            Call<int>(recent,4,first_recent_id,&placeholder);
            Call<int>(recent,36,first_recent_id,0);
            recent->base.release(&recent->base);
        }
        Call<int>(child,36,recent_id,0);
        Call<int>(child,32,recent_id,0);
    }
    item(flac_id,state.flac ? L"FLAC (Enabled)" : L"FLAC (Disabled)",state.flac,true);
    item(ogg_id,state.ogg ? L"Ogg (Enabled)" : L"Ogg (Disabled)",state.ogg,true);
    item(lyrics_id,state.lyrics ? L"Lyrics (Enabled)" : L"Lyrics (Disabled)",state.lyrics,true);
    UpdateQuality(child);
    child->base.release(&child->base);
    if(inserted) history::HistoryLog("To Disk inserted into Spotify main menu: Downloads, Save Location, FLAC, Ogg, Lyrics");
}
static Menu* AddSubmenuHook(Menu* menu,int id,const String* text) {
    auto callback=menu_callbacks.Enter();
    Menu* result=original_add_submenu(menu,id,text);
    // MenuWillShow occurs after CEF has built the visible menu. Insert while
    // Spotify constructs the model so the first opening contains our items.
    static thread_local bool populating=false;
    if(!populating) { populating=true; Populate(menu); populating=false; }
    return result;
}
struct Wrapped {
    Delegate api;
    std::atomic<unsigned> refs{1};
    Delegate* original=nullptr;
    MenuCapabilities capability;
    // The same transferred delegate serves the root and its child views.
    // Keep command targets paired with the labels CEF built for this opening.
    std::vector<history::RecentDownload> recent;
    // Formatting can repeat for accessibility while the menu is displayed.
    bool recent_open=false;
    history::PlaybackQualitySnapshot song{};
    std::wstring song_file;
    bool song_bound=false;
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
static void BindSong(Wrapped* wrapped) {
    const auto snapshot=history::ReadPlaybackQuality();
    std::wstring saved_file;
    history::ReadSongDestination(snapshot.identity,history::GetSettings().generation,saved_file);
    wrapped->song_file.swap(saved_file);
    wrapped->song=snapshot;
    wrapped->song_bound=true;
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
    auto* recent=Call<Menu*>(child,28,recent_id);
    if(!recent) return;
    if(!wrapped->CapableCached(recent)) {ReleaseMenu(recent);return;}
    if(!Call<int>(recent,1)) {ReleaseMenu(recent);return;}
    wrapped->recent.swap(next);
    if(wrapped->recent.empty()) {
        wchar_t empty[]=L"No downloads yet";String text{empty,15,nullptr};
        Call<int>(recent,4,first_recent_id,&text);
        Call<int>(recent,36,first_recent_id,0);
    } else for(size_t i=0;i<wrapped->recent.size();++i) {
        auto text=Text(labels[i]);Call<int>(recent,4,first_recent_id+int(i),&text);
    }
    Call<int>(child,36,recent_id,int(!wrapped->recent.empty()));
    Call<int>(child,32,recent_id,int(!wrapped->recent.empty()));
    ReleaseMenu(recent);
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
        L"Explorer could not show this download.",L"Latest Downloads >",MB_OK|MB_ICONINFORMATION);
    if(SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
static void RevealRecent(const std::wstring& path) {
    auto* copy=new(std::nothrow) std::wstring(path);
    if(!copy) return;
    // Filesystem and shell work belongs to a click, away from CEF's UI thread.
    HANDLE worker=CreateThread(nullptr,0,RevealRecentWorker,copy,0,nullptr);
    if(worker) CloseHandle(worker);
    else {delete copy;MessageBoxW(GetActiveWindow(),L"Explorer could not be started.",L"Latest Downloads >",MB_OK|MB_ICONINFORMATION);}
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
    if(command==song_id) {
        try {if(!Self(self)->song_file.empty()) RevealSong(Self(self)->song_file);}
        catch(...) {/* Keep allocation failures inside the CEF callback. */}
        ReleaseMenu(menu);return;
    }
    if(command>=first_recent_id && command<first_recent_id+int(history::kRecentDownloadLimit)) {
        const auto index=size_t(command-first_recent_id);
        try {if(index<Self(self)->recent.size()) RevealRecent(Self(self)->recent[index].path);}
        catch(...) {/* CEF callbacks must not propagate allocation failures. */}
        ReleaseMenu(menu);return;
    }
    if(command==recent_id) {ReleaseMenu(menu);return;}
    if(command>=song_id && command<=last_quality_id) {ReleaseMenu(menu);return;}
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
            Call<int>(menu,20,command,&text); Call<int>(menu,40,command,int(value));
            char line[160]; snprintf(line,sizeof(line),"To Disk command=%d accepted=%d enabled=%d",command,ok,value); history::HistoryLog(line);
        }
        if(!ok) MessageBoxW(GetActiveWindow(),L"The setting could not be saved.",L"To Disk",MB_OK|MB_ICONERROR);
        ReleaseMenu(menu);
        return;
    }
    if(w->original && w->original->execute) w->original->execute(w->original,menu,command,flags);
    else ReleaseMenu(menu);
}
static void Outside(Delegate* self,Menu* menu,const void* point) { auto* o=Self(self)->original; if(o && o->outside) o->outside(o,menu,point); else ReleaseMenu(menu); }
static void Open(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->open) o->open(o,menu,rtl); else ReleaseMenu(menu); }
static void Close(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->close) o->close(o,menu,rtl); else ReleaseMenu(menu); }
static void WillShow(Delegate* self,Menu* menu) {
    if(Self(self)->CapableCached(menu) && Call<int>(menu,15,root_id)>=0) Self(self)->recent_open=true;
    auto* o=Self(self)->original;
    // CEF transfers one menu reference into every delegate callback. Retain
    // another across the original callback, which consumes its argument.
    if(o && o->will_show) { menu->base.add_ref(&menu->base); o->will_show(o,menu); }
    if(Self(self)->CapableCached(menu) && Call<int>(menu,15,song_id)>=0) {
        if(!Self(self)->song_bound) BindSong(Self(self));
        UpdateQuality(menu,&Self(self)->song,&Self(self)->song_file);
    }
    else Populate(menu);
    ReleaseMenu(menu);
}
static void Closed(Delegate* self,Menu* menu) {
    if(Self(self)->CapableCached(menu) && Call<int>(menu,15,root_id)>=0) Self(self)->recent_open=false;
    Self(self)->ClearValidation(); auto* o=Self(self)->original; if(o && o->closed) o->closed(o,menu); else ReleaseMenu(menu); }
static int Format(Delegate* self,Menu* menu,String* label) {
    auto* o=Self(self)->original;int changed=0;
    if(o && o->format) {
        menu->base.add_ref(&menu->base);
        changed=o->format(o,menu,label);
    }
    // Refresh visibility while CEF formats the parent label, before it builds
    // the submenu's visible children. WillShow alone is too late for the first
    // opening after a codec change on a reused model.
    if(label && label->str && label->length==7 && !wmemcmp(label->str,L"To Disk",7) &&
       Self(self)->CapableCached(menu) && Call<int>(menu,15,root_id)>=0) {
        if(auto* child=Call<Menu*>(menu,28,root_id)) {
            try { if(Self(self)->CapableCached(child) && Call<int>(child,15,song_id)>=0 &&
                     Call<int>(child,15,downloads_id)>=0) {
                         if(!Self(self)->recent_open) BindSong(Self(self));
                         UpdateQuality(child,&Self(self)->song,&Self(self)->song_file);
                         if(!Self(self)->recent_open) RefreshRecent(Self(self),child);
                     } }
            catch(...) {/* Keep prior visibility on allocation failure. */}
            ReleaseMenu(child);
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
            if(!Self(self)->CapableCached(menu) || Call<int>(menu,15,song_id)<0 ||
               Call<int>(menu,15,downloads_id)<0) break;
            try {
                if(i==0 && (!Self(self)->recent_open || !Self(self)->song_bound)) BindSong(Self(self));
                const auto rows=history::PlaybackQualityLabels(i==0 ? Self(self)->song : history::ReadPlaybackQuality());
                if(i==0) Call<int>(menu,36,song_id,int(!Self(self)->song_file.empty()));
                if(set_string(rows[i].data(),rows[i].size(),label,1))changed=1;
            }catch(...) {/* Keep the original CEF label on allocation failure. */}
            break;
        }
    }
    ReleaseMenu(menu);return changed;
}
static Menu* Hook(Delegate* delegate) {
    auto callback=menu_callbacks.Enter();
    if(!delegate || !cef_menu::SupportsDelegate(delegate->base.size) ||
       !Executable(reinterpret_cast<const void*>(delegate->base.add_ref)) ||
       !Executable(reinterpret_cast<const void*>(delegate->base.release)))
        return original_create(delegate);
    size_t delegate_size=delegate->base.size;
    auto* w=new(std::nothrow) Wrapped;
    if(!w) return original_create(delegate);
    w->api={{sizeof(Delegate),Add,Release,One,Any},Execute,Outside,Open,Close,WillShow,Closed,Format};
    // The incoming delegate and the replacement each carry a transferred
    // reference. CEF Wrap() consumes it; releasing again here frees a live
    // callback object. Hold the original transferred reference until teardown.
    w->original=delegate;
    Menu* result=original_create(&w->api);
    if(Capable(result) && submenu_init.TryBegin(GetTickCount64())) {
        void* address=result->methods[7];
        MH_STATUS status=address ? MH_CreateHook(address,reinterpret_cast<void*>(AddSubmenuHook),reinterpret_cast<void**>(&original_add_submenu)) : MH_ERROR_NOT_EXECUTABLE;
        bool created=status==MH_OK;
        if(status==MH_OK) status=MH_EnableHook(address);
        if(status==MH_OK) submenu_init.Activate();
        else {
            if(created) MH_RemoveHook(address);
            submenu_init.Retry(GetTickCount64());
        }
        char line[180]; snprintf(line,sizeof(line),"CEF main-menu construction hook: %s%s",MH_StatusToString(status),status==MH_OK?"":"; retry scheduled"); history::HistoryLog(line);
    }
    static volatile LONG logged=0;
    if(InterlockedIncrement(&logged)<=8) {
        char line[160]; snprintf(line,sizeof(line),"CEF menu model wrapped delegate_bytes=%zu model_bytes=%zu",
            delegate_size,result ? result->base.size : 0); history::HistoryLog(line);
    }
    return result;
}
}
static void FinishMenuStartup(bool applied,bool activation_attempted) {
    if(!menu_prepared)return;
    menu_prepared=false;
    if(applied){menu_init.Activate();history::HistoryLog("runtime-capability CEF main-menu integration: MH_OK (shared startup activation)");return;}
    const auto rollback=hooks::RollbackStartupHooks(menu_startup_hooks,[]{
        for(unsigned waited=0;menu_callbacks.Active()&&waited<5000;++waited)Sleep(1);
        return menu_callbacks.Active()==0;
    },!activation_attempted);
    // A thread can be entering a detour before its callback counter increments.
    // After any activation attempt retain all trampolines, even with no submenu.
    // A partially applied root may already have installed the submenu hook.
    // Its callback/delegate state must survive even if root cleanup succeeded.
    if(!activation_attempted&&rollback.CanRelease()&&!original_add_submenu){
        menu_init.Retry(GetTickCount64());history::HistoryLog("To Disk startup activation failed; rollback complete; retry scheduled");
    } else {
        menu_init.MarkUnsupported();history::HistoryLog("To Disk startup activation failed or rollback unsafe; trampoline state preserved and retries disabled");
    }
}
void FinalizeToDiskMenu(bool applied) {FinishMenuStartup(applied,true);}
bool PrepareToDiskMenu(HMODULE cef) {
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!menu_init.TryBegin(now)) return false;
    if(!history::GetSettings().menu) { history::HistoryLog("To Disk menu disabled by INI Menu=0"); menu_init.MarkUnsupported(); return false; }
    auto string_set=GetProcAddress(cef,"cef_string_utf16_set");
    memcpy(&set_string,&string_set,sizeof(set_string));
    if(!set_string)history::HistoryLog("To Disk live labels unavailable: cef_string_utf16_set missing");
    auto create=GetProcAddress(cef,"cef_menu_model_create");
    if(!create || !Executable(reinterpret_cast<const void*>(create))) {
        history::HistoryLog("To Disk menu unavailable: cef_menu_model_create capability missing"); menu_init.MarkUnsupported(); return false;
    }
    menu_prepared=true;
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=MH_CreateHook(reinterpret_cast<void*>(create),reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original_create));
    if(status==MH_OK)menu_startup_hooks.Add(reinterpret_cast<void*>(create));
    if(status!=MH_OK||!hooks::QueueStartupHooks(menu_startup_hooks)){
        char line[180];snprintf(line,sizeof(line),"To Disk startup hook preparation failed: %s",MH_StatusToString(status));history::HistoryLog(line);
        FinishMenuStartup(false,false);return false;
    }
    return true;
}
void StartToDiskMenu(HMODULE cef) {
    if(PrepareToDiskMenu(cef))FinalizeToDiskMenu(MH_ApplyQueued()==MH_OK);
}
