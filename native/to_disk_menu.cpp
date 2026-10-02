#define WIN32_LEAN_AND_MEAN
#include "to_disk_menu.h"
#include "cef_menu_capability.h"
#include "hook_init_state.h"
#include "history_settings.h"
#include "playback_quality.h"
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
hooks::InitController submenu_init;
constexpr int root_id=28480,downloads_id=28481,location_id=28482,flac_id=28483,ogg_id=28484;
constexpr int song_id=28485,last_quality_id=song_id+3;
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
static bool Main(Menu* menu) {
    if(!Capable(menu) || Call<int>(menu,0)) return false;
    const size_t count=Call<size_t>(menu,2); if(count<3 || count>30) return false;
    int types[30]{};
    for(size_t index=0;index<count;++index) types[index]=Call<int>(menu,23,index);
    return cef_menu::LooksLikeMainMenu(false,types,count);
}
static void UpdateQuality(Menu* menu) {
    const auto labels=history::PlaybackQualityLabels(history::ReadPlaybackQuality());
    if(Call<int>(menu,15,song_id)<0) Call<int>(menu,3);
    for(unsigned i=0;i<labels.size();++i) {
        int id=song_id+int(i);auto text=Text(labels[i]);
        if(Call<int>(menu,15,id)<0)Call<int>(menu,4,id,&text);
        else Call<int>(menu,20,id,&text);
        Call<int>(menu,36,id,0);
    }
}
static void Populate(Menu* menu) {
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
    item(flac_id,state.flac ? L"FLAC (Enabled)" : L"FLAC (Disabled)",state.flac,true);
    item(ogg_id,state.ogg ? L"Ogg (Enabled)" : L"Ogg (Disabled)",state.ogg,true);
    UpdateQuality(child);
    child->base.release(&child->base);
    if(inserted) history::HistoryLog("To Disk inserted into Spotify main menu: Downloads, Save Location, FLAC, Ogg");
}
static Menu* AddSubmenuHook(Menu* menu,int id,const String* text) {
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
    ~Wrapped() { if(original) original->base.release(&original->base); }
};
static Wrapped* Self(void* p) { return static_cast<Wrapped*>(p); }
static void Add(Base* p) { Self(p)->refs.fetch_add(1,std::memory_order_relaxed); }
static int Release(Base* p) { auto* w=Self(p); if(w->refs.fetch_sub(1,std::memory_order_acq_rel)==1) { delete w; return 1; } return 0; }
static int One(Base* p) { return Self(p)->refs.load()==1; }
static int Any(Base* p) { return Self(p)->refs.load()!=0; }
static void ReleaseMenu(Menu* menu) { if(menu && menu->base.release) menu->base.release(&menu->base); }
static void Execute(Delegate* self,Menu* menu,int command,int flags) {
    if(command>=song_id && command<=last_quality_id) {ReleaseMenu(menu);return;}
    auto* w=Self(self); auto settings=history::GetSettings(); bool handled=true,ok=true;
    switch(command) {
        case downloads_id: ok=history::SetDownloads(!settings.downloads); break;
        case ogg_id: ok=history::SetOgg(!settings.ogg); break;
        case location_id: history::PickSaveLocation(GetActiveWindow()); break;
        case flac_id: ok=history::SetFlac(!settings.flac); break;
        default: handled=false;
    }
    if(handled) {
        if(command==downloads_id || command==ogg_id || command==flac_id) {
            auto updated=history::GetSettings();
            bool value=command==downloads_id ? updated.downloads : command==ogg_id ? updated.ogg : updated.flac;
            std::wstring label=command==downloads_id ? L"Downloads" : command==ogg_id ? L"Ogg" : L"FLAC";
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
    auto* o=Self(self)->original;
    // CEF transfers one menu reference into every delegate callback. Retain
    // another across the original callback, which consumes its argument.
    if(o && o->will_show) { menu->base.add_ref(&menu->base); o->will_show(o,menu); }
    if(Capable(menu) && Call<int>(menu,15,song_id)>=0) UpdateQuality(menu);
    else Populate(menu);
    ReleaseMenu(menu);
}
static void Closed(Delegate* self,Menu* menu) { auto* o=Self(self)->original; if(o && o->closed) o->closed(o,menu); else ReleaseMenu(menu); }
static int Format(Delegate* self,Menu* menu,String* label) {
    auto* o=Self(self)->original;int changed=0;
    if(o && o->format) {
        menu->base.add_ref(&menu->base);
        changed=o->format(o,menu,label);
    }
    // CEF constructs visible labels before MenuWillShow. Its public label
    // formatter supplies fresh text during construction, on the UI thread.
    if(set_string && label && label->str && Capable(menu) &&
       Call<int>(menu,15,song_id)>=0 && Call<int>(menu,15,downloads_id)>=0) {
        const wchar_t* prefixes[]={L"Song: ",L"Quality: ",L"Format: ",L"Sample rate: "};
        for(unsigned i=0;i<4;++i) {
            const auto length=wcslen(prefixes[i]);
            if(label->length>=length && !wmemcmp(label->str,prefixes[i],length)) {
                try {
                    const auto rows=history::PlaybackQualityLabels(history::ReadPlaybackQuality());
                    if(set_string(rows[i].data(),rows[i].size(),label,1))changed=1;
                }catch(...) {/* Keep the original CEF label on allocation failure. */}
                break;
            }
        }
    }
    ReleaseMenu(menu);return changed;
}
static Menu* Hook(Delegate* delegate) {
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
void StartToDiskMenu(HMODULE cef) {
    const auto now=static_cast<std::uint64_t>(GetTickCount64());
    if(!menu_init.TryBegin(now)) return;
    if(!history::GetSettings().menu) { history::HistoryLog("To Disk menu disabled by INI Menu=0"); menu_init.MarkUnsupported(); return; }
    auto string_set=GetProcAddress(cef,"cef_string_utf16_set");
    memcpy(&set_string,&string_set,sizeof(set_string));
    if(!set_string)history::HistoryLog("To Disk live labels unavailable: cef_string_utf16_set missing");
    auto create=GetProcAddress(cef,"cef_menu_model_create");
    if(!create || !Executable(reinterpret_cast<const void*>(create))) {
        history::HistoryLog("To Disk menu unavailable: cef_menu_model_create capability missing"); menu_init.MarkUnsupported(); return;
    }
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=MH_CreateHook(reinterpret_cast<void*>(create),reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original_create));
    bool created=status==MH_OK;
    if(status==MH_OK) status=MH_EnableHook(reinterpret_cast<void*>(create));
    if(status==MH_OK) menu_init.Activate();
    else {if(created)MH_RemoveHook(reinterpret_cast<void*>(create));menu_init.Retry(now);}
    char line[220]; snprintf(line,sizeof(line),"runtime-capability CEF main-menu integration: %s%s",MH_StatusToString(status),status==MH_OK?"":"; retry scheduled"); history::HistoryLog(line);
}
