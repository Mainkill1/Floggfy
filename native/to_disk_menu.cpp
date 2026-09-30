#define WIN32_LEAN_AND_MEAN
#include "to_disk_menu.h"
#include "history_settings.h"
#include "vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <new>

namespace {
// Public CEF 146 API. Method order matches the pinned 8219561 C++ headers
// and the CEF translator's C layout. Guard every returned structure size.
struct Base { size_t size; void(*add_ref)(Base*); int(*release)(Base*);
    int(*has_one_ref)(Base*); int(*has_at_least_one_ref)(Base*); };
struct String { wchar_t* str; size_t length; void(*dtor)(wchar_t*); };
struct Menu { Base base; void* methods[56]; };
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
static_assert(sizeof(Base)==40 && sizeof(Menu)==488 && sizeof(Delegate)==96,"CEF146 x64 ABI");
using Create=Menu*(*)(Delegate*);
Create original_create;
using AddSubmenu=Menu*(*)(Menu*,int,const String*);
AddSubmenu original_add_submenu;
using FreeString=void(*)(String*);
FreeString free_string;
constexpr int root_id=28480,downloads_id=28481,location_id=28482,flac_id=28483,ogg_id=28484;
template<class R,class...A> R Call(Menu* m,unsigned slot,A...a) {
    if(!m || m->base.size!=sizeof(Menu) || slot>=56 || !m->methods[slot]) return R{};
    return reinterpret_cast<R(*)(Menu*,A...)>(m->methods[slot])(m,a...);
}
static String Text(const std::wstring& s) { return {const_cast<wchar_t*>(s.c_str()),s.size(),nullptr}; }
static std::wstring Label(Menu* m,size_t index) {
    String* text=Call<String*>(m,19,index);
    if(!text) return {};
    std::wstring value(text->str,text->length); free_string(text);
    return value;
}
static bool Main(Menu* m) {
    if(!m || m->base.size!=sizeof(Menu) || Call<int>(m,0)) return false;
    size_t n=Call<size_t>(m,2); if(n<3 || n>30) return false;
    bool file=false,edit=false,view=false;
    for(size_t i=0;i<n;i++) {
        auto s=Label(m,i); std::wstring normalized;
        for(auto ch:s) if(ch!=L'&' && ch!=L' ') normalized+=wchar_t(towlower(ch));
        file |= normalized==L"file"; edit |= normalized==L"edit"; view |= normalized==L"view";
    }
    return file && edit && view;
}
static void Populate(Menu* menu) {
    if(!Main(menu)) return;
    auto state=history::GetSettings();
    Menu* child=Call<Menu*>(menu,28,root_id);
    bool inserted=false;
    if(!child) {
        if(Call<int>(menu,15,root_id)>=0) return; // conflicting command id
        std::wstring label=L"To Disk"; auto text=Text(label);
        child=Call<Menu*>(menu,7,root_id,&text); inserted=true;
    }
    if(!child || child->base.size!=sizeof(Menu)) { if(child) child->base.release(&child->base); return; }
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
    Populate(menu); ReleaseMenu(menu);
}
static void Closed(Delegate* self,Menu* menu) { auto* o=Self(self)->original; if(o && o->closed) o->closed(o,menu); else ReleaseMenu(menu); }
static int Format(Delegate* self,Menu* menu,String* label) { auto* o=Self(self)->original; if(o && o->format) return o->format(o,menu,label); ReleaseMenu(menu); return 0; }
static Menu* Hook(Delegate* delegate) {
    if(!delegate || delegate->base.size!=sizeof(Delegate) || !delegate->base.add_ref || !delegate->base.release)
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
    static volatile LONG add_hook_attempted=0;
    if(result && result->base.size==sizeof(Menu) && InterlockedCompareExchange(&add_hook_attempted,1,0)==0) {
        void* address=result->methods[7];
        auto status=MH_CreateHook(address,reinterpret_cast<void*>(AddSubmenuHook),reinterpret_cast<void**>(&original_add_submenu));
        if(status==MH_OK) status=MH_EnableHook(address);
        char line[160]; snprintf(line,sizeof(line),"CEF main-menu construction hook: %s",MH_StatusToString(status)); history::HistoryLog(line);
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
    static bool attempted=false; if(attempted) return; attempted=true;
    if(!history::GetSettings().menu) { history::HistoryLog("To Disk menu disabled by INI Menu=0"); return; }
    using Version=int(*)(int);
    Version version=nullptr; FARPROC symbol=GetProcAddress(cef,"cef_version_info");
    memcpy(&version,&symbol,sizeof(version));
    auto create=GetProcAddress(cef,"cef_menu_model_create");
    symbol=GetProcAddress(cef,"cef_string_userfree_utf16_free");
    memcpy(&free_string,&symbol,sizeof(free_string));
    if(!version || version(0)!=146 || version(1)!=0 || version(2)!=10 || !create || !free_string) { history::HistoryLog("To Disk menu disabled: unsupported CEF version or ABI"); return; }
    MH_STATUS status=MH_Initialize(); if(status==MH_ERROR_ALREADY_INITIALIZED) status=MH_OK;
    if(status==MH_OK) status=MH_CreateHook(reinterpret_cast<void*>(create),reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original_create));
    if(status==MH_OK) status=MH_EnableHook(reinterpret_cast<void*>(create));
    char line[180]; snprintf(line,sizeof(line),"CEF146 main-menu integration: %s",MH_StatusToString(status)); history::HistoryLog(line);
}
