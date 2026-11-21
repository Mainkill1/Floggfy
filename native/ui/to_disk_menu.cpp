#define WIN32_LEAN_AND_MEAN
#include "to_disk_menu.h"
#include "cef_menu_capability.h"
#include "cef_menu_abi.h"
#include "../tooling/hook_init_state.h"
#include "../tooling/hook_startup_minhook.h"
#include "../tooling/history_settings.h"
#include "../tooling/network_kill_switch.h"
#include "../tooling/network_reconnect.h"
#include "../tooling/update_blocker.h"
#include "../tooling/async_log.h"
#include "../tooling/hook_diagnostics.h"
#include "playback_labels.h"
#include "recent_downloads.h"
#include "marked_catalog.h"
#include "song_destination.h"
#include <shellapi.h>
#include <shlobj.h>
#include <memory>
#include "../vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
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
constexpr int song_id=28485,last_quality_id=song_id+int(history::kPlaybackLabelPrefixes.size())-1,lyrics_id=28489;
constexpr int recent_id=28490,first_recent_id=28491;
constexpr unsigned speeds[]{1,2,4,8,10};
constexpr int speed_root_id=28474,first_speed_id=28475,last_speed_id=first_speed_id+int(std::size(speeds))-1;
constexpr int podcasts_id=28473,auto_skip_id=28472,never_pause_id=28471;
constexpr int fallbacks_root_id=28470,windows_fallback_id=28469,misc_root_id=28468,network_kill_id=28467;
constexpr int formats_root_id=28466,aac_id=28465,block_updates_id=28464;
constexpr int project_id=28463;
constexpr int marked_id=28458,marked_rebuild_id=28457;
constexpr wchar_t root_label[]=L"To Disk";
constexpr wchar_t recent_label[]=L"Latest Downloads";
constexpr wchar_t project_label[]=L"Floggfy - v1.4";
struct ToggleItem {
    int id;
    const wchar_t* label;
    const char* log_name;
    bool history::Settings::*enabled;
    bool (*set)(bool);
};
constexpr ToggleItem fallback_items[]={
    {28462,L"Read Spotify Disk Cache","Read Spotify Disk Cache",&history::Settings::read_spotify_disk_cache,history::SetReadSpotifyDiskCache},
    {28461,L"Fetch Missing Metadata","Fetch Missing Metadata",&history::Settings::fetch_missing_metadata,history::SetFetchMissingMetadata},
    {28460,L"Fetch Missing Artwork","Fetch Missing Artwork",&history::Settings::fetch_missing_artwork,history::SetFetchMissingArtwork},
    {28459,L"Fetch Missing Lyrics","Fetch Missing Lyrics",&history::Settings::fetch_missing_lyrics,history::SetFetchMissingLyrics},
    {windows_fallback_id,L"Windows Playback Fallback","Windows Playback Fallback",
     &history::Settings::windows_playback_fallback,history::SetWindowsPlaybackFallback},
};
constexpr ToggleItem format_items[]={
    {ogg_id,L"Ogg","Ogg",&history::Settings::ogg,history::SetOgg},
    {flac_id,L"FLAC","FLAC",&history::Settings::flac,history::SetFlac},
    {aac_id,L"AAC","AAC",&history::Settings::aac,history::SetAAC},
    {podcasts_id,L"Podcasts","Podcasts",&history::Settings::podcasts,history::SetPodcasts},
    {lyrics_id,L"Include Lyrics","Lyrics",&history::Settings::lyrics,history::SetLyrics},
};
constexpr ToggleItem download_item{downloads_id,L"Downloads","Downloads",&history::Settings::downloads,history::SetDownloads};
constexpr ToggleItem auto_skip_item{auto_skip_id,L"Auto Skip Downloaded","Auto Skip Downloaded",
    &history::Settings::auto_skip_downloaded,history::SetAutoSkipDownloaded};
static const ToggleItem* ToggleFor(int command) noexcept {
    if(command==downloads_id)return &download_item;
    if(command==auto_skip_id)return &auto_skip_item;
    for(const auto& entry:format_items)if(entry.id==command)return &entry;
    for(const auto& entry:fallback_items)if(entry.id==command)return &entry;
    return nullptr;
}
static_assert(fallback_items[3].id>=cef_menu::kUserCommandFirst &&
              first_recent_id+history::kRecentDownloadLimit-1<=cef_menu::kUserCommandLast,
              "Menu commands stay within the CEF user command range (26500..28500)");
static String Text(std::wstring_view s) { return {const_cast<wchar_t*>(s.data()),s.size(),nullptr}; }
static void AddCheckItem(Menu* menu,int id,std::wstring_view label,bool checked,bool enabled=true) {
    const auto text=Text(label);
    MenuMethods::AddCheckItem(menu,id,&text);
    MenuMethods::SetChecked(menu,id,checked);
    MenuMethods::SetEnabled(menu,id,enabled);
}
static void AddDisabledItem(Menu* menu,int id,std::wstring_view label) {
    const auto text=Text(label);
    MenuMethods::AddItem(menu,id,&text);
    MenuMethods::SetEnabled(menu,id,false);
}
static std::wstring_view ProtectionLabel(int command,bool available) {
    if(command==block_updates_id) return available ? L"Block Updates" : L"Block Updates (Unavailable)";
    return available ? L"Network Kill Switch" : L"Network Kill Switch (Unavailable)";
}
static const char* SettingName(int command) noexcept {
    if(auto* item=ToggleFor(command))return item->log_name;
    if(command==never_pause_id)return "Never Pause";
    if(command==network_kill_id)return "Network Kill Switch";
    return "Block Updates";
}
static void SettingOutcomeLine(int command,bool accepted,bool enabled,char* line,size_t size) noexcept {
    if(accepted)snprintf(line,size,"%s %s",SettingName(command),enabled?"enabled":"disabled");
    else snprintf(line,size,"%s change failed; remains %s",SettingName(command),enabled?"enabled":"disabled");
}
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
    const size_t count=MenuMethods::Count(menu); if(count<3 || count>30) return false;
    int types[30]{};
    for(size_t index=0;index<count;++index) types[index]=MenuMethods::TypeAt(menu,index);
    return cef_menu::LooksLikeMainMenu(false,types,count);
}
static void UpdateQuality(Menu* menu,const history::PlaybackQualitySnapshot* song=nullptr,bool song_available=false) {
    const auto snapshot=history::ReadPlaybackQuality();
    const auto labels=history::PlaybackQualityLabels(snapshot,song);
    const bool known_format=snapshot.format!=history::PlaybackFormat::Unknown;
    const bool visible[]{true,snapshot.level!=history::PlaybackLevel::Unknown,known_format,known_format && snapshot.rate!=0};
    if(MenuMethods::IndexOf(menu,song_id)<0) MenuMethods::AddSeparator(menu);
    for(unsigned i=0;i<labels.size();++i) {
        int id=song_id+int(i);auto text=Text(labels[i]);
        if(MenuMethods::IndexOf(menu,id)<0)MenuMethods::AddItem(menu,id,&text);
        else MenuMethods::SetLabel(menu,id,&text);
        MenuMethods::SetEnabled(menu,id,i==0 && song_available);
        MenuMethods::SetVisible(menu,id,visible[i]);
    }
}
static void RefreshSpeedMenu(Menu* parent) {
    auto* speed_menu=MenuMethods::GetSubmenu(parent,speed_root_id);
    if(!speed_menu)return;
    MenuReference speed_reference(speed_menu);
    const auto selected=history::CaptureSpeed();
    for(unsigned i=0;i<std::size(speeds);++i) {
        const auto id=first_speed_id+int(i);
        if(MenuMethods::IndexOf(speed_menu,id)<0)continue;
        MenuMethods::SetChecked(speed_menu,id,selected==speeds[i]);
        MenuMethods::SetEnabled(speed_menu,id,history::CaptureSpeedAvailable(speeds[i]));
    }
}
static void RefreshMarkedMenu(Menu* menu,bool enabled) {
    MenuMethods::SetChecked(menu,marked_id,enabled);
    MenuMethods::SetChecked(menu,marked_rebuild_id,history::MarkedRebuildRunning());
    MenuMethods::SetEnabled(menu,marked_rebuild_id,enabled);
}
static bool ShowCaptureSpeed(const history::Settings& state) noexcept {
    return state.capture_speed>=0;
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
            history::QueueDiagnostic("To Disk menu probe: non-main model ignored");
        return;
    }
    const auto state=history::GetSettings();
    auto submenu=[](Menu* parent,int id,std::wstring_view label) {
        const auto text=Text(label);
        return MenuMethods::AddSubmenu(parent,id,&text);
    };
    auto* child=submenu(menu,root_id,root_label);
    MenuReference child_reference(child);
    if(!Capable(child))return;
    AddDisabledItem(child,project_id,project_label);
    MenuMethods::AddSeparator(child);
    auto add_toggle=[&](Menu* target,const ToggleItem& item) {
        AddCheckItem(target,item.id,item.label,state.*item.enabled);
    };
    auto add_toggle_submenu=[&](int id,std::wstring_view label,const auto& items) {
        if(auto* target=submenu(child,id,label)) {
            MenuReference reference(target);
            for(const auto& item:items)add_toggle(target,item);
        }
    };
    add_toggle(child,download_item);
    if(auto* recent=submenu(child,recent_id,recent_label)) {
        MenuReference recent_reference(recent);
        AddDisabledItem(recent,first_recent_id,L"No downloads yet");
    }
    MenuMethods::SetEnabled(child,recent_id,0);
    MenuMethods::SetVisible(child,recent_id,0);
    if(ShowCaptureSpeed(state)) {
        if(auto* speed_menu=submenu(child,speed_root_id,L"Capture Speed")) {
            MenuReference speed_reference(speed_menu);
            for(unsigned i=0;i<std::size(speeds);++i) {
                const auto speed_label=std::to_wstring(speeds[i])+L"x";auto speed_text=Text(speed_label);
                MenuMethods::AddCheckItem(speed_menu,first_speed_id+int(i),&speed_text);
            }
        }
    }
    if(state.never_pause>=0)
        AddCheckItem(child,never_pause_id,L"Never Pause",state.never_pause==1);
    add_toggle_submenu(formats_root_id,L"Allowed Formats",format_items);
    if(auto* misc=submenu(child,misc_root_id,L"Misc")) {
        MenuReference misc_reference(misc);
        const auto location_text=Text(L"Save Location");
        MenuMethods::AddItem(misc,location_id,&location_text);
        add_toggle(misc,auto_skip_item);
        AddCheckItem(misc,marked_id,L"Marked When Downloaded",state.marked_when_downloaded);
        AddCheckItem(misc,marked_rebuild_id,L"Marked DB Rebuild",history::MarkedRebuildRunning(),state.marked_when_downloaded);
        const bool can_block_updates=updates::Available();
        AddCheckItem(misc,block_updates_id,ProtectionLabel(block_updates_id,can_block_updates),updates::Blocked(),can_block_updates);
        if(state.network_kill_switch>=0) {
            const bool can_kill_network=network::Available();
            AddCheckItem(misc,network_kill_id,ProtectionLabel(network_kill_id,can_kill_network),network::Enabled(),can_kill_network);
        }
    }
    add_toggle_submenu(fallbacks_root_id,L"Fallbacks",fallback_items);
    RefreshSpeedMenu(child);
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

        void RefreshSong(bool bind) {
            if(bind) {
                song=history::ReadPlaybackQuality();
                song_settings_generation=history::GetSettings().generation;
                song_file.clear();
                song_bound=true;
            }
            if(song_bound && song_file.empty())
                history::ReadSongDestination(song.identity,song_settings_generation,song_file);
        }
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
    bool HasQualityRows(Menu* menu) {
        return CapableCached(menu) && MenuMethods::IndexOf(menu,song_id)>=0 && MenuMethods::IndexOf(menu,downloads_id)>=0;
    }
    void ClearValidation() { capability.ClearValidation(); }
    ~Wrapped() { if(original) original->base.release(&original->base); }
};
static Wrapped* Self(void* p) { return static_cast<Wrapped*>(p); }
static void Add(Base* p) { Self(p)->refs.fetch_add(1,std::memory_order_relaxed); }
static int Release(Base* p) { auto* w=Self(p); if(w->refs.fetch_sub(1,std::memory_order_acq_rel)==1) { delete w; return 1; } return 0; }
static int One(Base* p) { return Self(p)->refs.load()==1; }
static int Any(Base* p) { return Self(p)->refs.load()!=0; }
static void ReleaseMenu(Menu* menu) { MenuReference reference(menu); }
static void RefreshRecent(Wrapped* wrapped,Menu* child) {
    std::vector<history::RecentDownload> next;
    if(!history::ReadRecentDownloads(next)) return; // Never block or erase a prior snapshot.
    if(next.size()>history::kRecentDownloadLimit) next.resize(history::kRecentDownloadLimit);
    // Complete allocations before retaining or changing CEF models. The stored
    // snapshot keeps full titles and paths; only labels receive display hygiene.
    std::vector<std::wstring> labels;
    labels.reserve(next.size());
    for(const auto& entry:next) labels.push_back(entry.title.empty() ? L"Untitled download" : history::MenuTitle(entry.title,0x9f));
    auto* recent=MenuMethods::GetSubmenu(child,recent_id);
    MenuReference recent_reference(recent);
    if(!wrapped->CapableCached(recent) || !MenuMethods::Clear(recent))return;
    wrapped->opening.snapshot.recent.swap(next);
    if(labels.empty()) AddDisabledItem(recent,first_recent_id,L"No downloads yet");
    for(size_t i=0;i<labels.size();++i) {
        const auto text=Text(labels[i]);
        MenuMethods::AddItem(recent,first_recent_id+int(i),&text);
    }
    MenuMethods::SetEnabled(child,recent_id,!labels.empty());
    MenuMethods::SetVisible(child,recent_id,!labels.empty());
}
struct RevealRequest { std::wstring path; bool song_folder; };
static DWORD WINAPI RevealWorker(void* argument) {
    std::unique_ptr<RevealRequest> request(static_cast<RevealRequest*>(argument));
    const auto& path=request->path;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool available=false,opened=false;
    try {
        available=win32::IsFile(GetFileAttributesW(history::WindowsPath(path).c_str()));
        if(available && request->song_folder) {
            const auto slash=path.find_last_of(L"\\/");
            if(slash!=std::wstring::npos) {
                const auto folder=path.substr(0,slash);
                opened=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
            }
        } else if(available) {
            PIDLIST_ABSOLUTE item=nullptr;
            opened=SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr)) &&
                SUCCEEDED(SHOpenFolderAndSelectItems(item,0,nullptr,0));
            CoTaskMemFree(item);
        }
    } catch(...) {/* Never unwind out of a click worker on allocation failure. */}
    if(!opened) {
        const wchar_t* message=request->song_folder
            ? (available ? L"Explorer could not open this song's folder." : L"This song's saved audio is no longer available at its saved location.")
            : (available ? L"Explorer could not show this download." : L"This download is no longer available at its saved location.");
        MessageBoxW(nullptr,message,request->song_folder ? L"Song folder" : recent_label,MB_OK|MB_ICONINFORMATION);
    }
    if(SUCCEEDED(initialized)) CoUninitialize();
    return 0;
}
static void RevealSavedFile(const std::wstring& path,bool song_folder) {
    auto* request=new(std::nothrow) RevealRequest{path,song_folder};
    if(!request)return;
    HANDLE worker=CreateThread(nullptr,0,RevealWorker,request,0,nullptr);
    if(worker)CloseHandle(worker);
    else {
        delete request;
        MessageBoxW(GetActiveWindow(),song_folder?L"The song folder could not be opened.":L"Explorer could not be started.",
                    song_folder?L"Song folder":recent_label,MB_OK|(song_folder?MB_ICONERROR:MB_ICONINFORMATION));
    }
}
static bool ChangeNetworkKillSwitch(bool enabled) {
    if(!network::Available())return false;
    if(history::GetSettings().network_kill_switch_telemetry_lock) {
        const bool started=network::RequestReconnect(enabled);
        if(started)history::LogEvent(history::LogKind::Activity,"network",enabled?
            "Network Kill Switch enabling with telemetry lock; restart scheduled":
            "Network Kill Switch reconnect scheduled; networking stays blocked until processes stop and queue cleanup finishes");
        if(!started)history::LogEvent(history::LogKind::Failure,"failed","Network Kill Switch restart could not start; networking remains blocked");
        return started;
    }
    const bool previous=network::Enabled();
    if(!network::SetEnabled(enabled))return false;
    try {
        if(history::SetNetworkKillSwitch(enabled))return true;
    }catch(...) {/* Restore networking if allocating the settings snapshot fails. */}
    if(!network::SetEnabled(previous))
        history::LogEvent(history::LogKind::Failure,"failed","Network Kill Switch rollback failed; live state differs from the saved preference");
    return false;
}
static void LogBlockUpdatesFailure(const char* stage,DWORD error) {
    char line[192];
    snprintf(line,sizeof(line),"Block Updates failed stage=%s error=%lu active=%d",stage,static_cast<unsigned long>(error),int(updates::Blocked()));
    history::LogEvent(history::LogKind::Failure,"failed",line);
}
static bool ChangeBlockUpdates(bool blocked) {
    if(!updates::Available()){LogBlockUpdatesFailure("unavailable",updates::LastError());return false;}
    const bool previous=updates::Blocked();
    if(!updates::SetBlocked(blocked)){LogBlockUpdatesFailure("apply",updates::LastError());return false;}
    try {
        if(history::SetBlockUpdates(blocked))return true;
    }catch(...) {/* Restore the folder policy if settings allocation fails. */}
    const bool restored=updates::SetBlocked(previous);
    const auto restore_error=restored ? ERROR_SUCCESS : updates::LastError();
    history::LogEvent(history::LogKind::Failure,"failed","Block Updates settings change rejected; previous protection restoration attempted");
    if(!restored)LogBlockUpdatesFailure("rollback",restore_error);
    return false;
}
static void Execute(Delegate* self,Menu* menu,int command,int flags) try {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuExecute);
    MenuReference reference(menu);
    auto* wrapped=Self(self);
    if(command==project_id)return;
    if(command==song_id) {
        const auto& file=wrapped->opening.snapshot.song_file;
        if(!file.empty())RevealSavedFile(file,true);
        return;
    }
    if(command>=first_recent_id && command<first_recent_id+int(history::kRecentDownloadLimit)) {
        const auto index=size_t(command-first_recent_id);
        const auto& recent=wrapped->opening.snapshot.recent;
        if(index<recent.size())RevealSavedFile(recent[index].path,false);
        return;
    }
    if(command==recent_id || command==speed_root_id || command==formats_root_id ||
       command==fallbacks_root_id || command==misc_root_id ||
       (command>song_id && command<=last_quality_id))return;
    if(command>=first_speed_id && command<=last_speed_id) {
        const auto speed=speeds[command-first_speed_id];
        if(!history::CaptureSpeedAvailable(speed)) {
            const auto active=history::CaptureSpeed();char line[128];
            snprintf(line,sizeof(line),"request=%ux rejected; active=%ux; acceleration disabled or unavailable",speed,active);
            history::LogEvent(history::LogKind::Warning,"capture-speed",line);
            MessageBoxW(GetActiveWindow(),L"Accelerated capture is disabled or unavailable; the current speed remains active.",L"To Disk",MB_OK|MB_ICONINFORMATION);
            return;
        }
        const bool ok=history::SetCaptureSpeed(speed);
        if(ok)for(int id=first_speed_id;id<=last_speed_id;++id)
            if(MenuMethods::IndexOf(menu,id)>=0)MenuMethods::SetChecked(menu,id,int(id==command));
        const auto active=history::CaptureSpeed();char line[160];
        snprintf(line,sizeof(line),"request=%ux accepted=%d active=%ux; replay the current track from the beginning",speed,ok,active);
        history::LogEvent(ok?history::LogKind::Activity:history::LogKind::Failure,ok?"settings":"failed",line);
        if(!ok)MessageBoxW(GetActiveWindow(),L"The capture speed could not be saved; the current speed remains active.",L"To Disk",MB_OK|MB_ICONERROR);
        return;
    }
    const auto settings=history::GetSettings();
    bool ok=true;
    if(command==marked_id||command==marked_rebuild_id) {
        if(command==marked_id)ok=history::SetMarkedWhenDownloaded(!settings.marked_when_downloaded);
        const auto updated=history::GetSettings();
        history::ConfigureMarkedCatalog(updated.root,updated.marked_when_downloaded);
        if(command==marked_rebuild_id)ok=history::ToggleMarkedRebuild();
        RefreshMarkedMenu(menu,updated.marked_when_downloaded);
        if(!ok)MessageBoxW(GetActiveWindow(),L"The marked-download setting or scan could not be started. Check the log for details.",L"To Disk",MB_OK|MB_ICONERROR);
        return;
    }
    if(command==location_id) {
        history::PickSaveLocation(GetActiveWindow());
        const auto updated=history::GetSettings();
        history::ConfigureMarkedCatalog(updated.root,updated.marked_when_downloaded);
        return;
    }
    bool enabled=false;
    const wchar_t* failure=L"The setting could not be saved.";
    if(const auto* item=ToggleFor(command)) {
        ok=item->set(!(settings.*(item->enabled)));
        enabled=history::GetSettings().*(item->enabled);
    } else if(command==never_pause_id) {
        if(settings.never_pause<0)return;
        ok=history::SetNeverPause(settings.never_pause==0);
        enabled=history::GetSettings().never_pause==1;
    } else if(command==block_updates_id || command==network_kill_id) {
        const bool updates_item=command==block_updates_id;
        if(!updates_item && settings.network_kill_switch<0)return;
        ok=updates_item ? ChangeBlockUpdates(!updates::Blocked()) : ChangeNetworkKillSwitch(!network::Enabled());
        enabled=updates_item ? updates::Blocked() : network::Enabled();
        const bool available=updates_item ? updates::Available() : network::Available();
        const auto text=Text(ProtectionLabel(command,available));
        MenuMethods::SetLabel(menu,command,&text);
        if(updates_item)MenuMethods::SetEnabled(menu,command,available);
        failure=updates_item ? L"Update blocking could not be changed. Check the log for details." :
                               L"The network kill switch could not be changed. Check the log for details.";
    } else {
        auto* original=wrapped->original;
        if(original && original->execute)original->execute(original,reference.Take(),command,flags);
        return;
    }
    MenuMethods::SetChecked(menu,command,enabled);
    char line[96];SettingOutcomeLine(command,ok,enabled,line,sizeof(line));
    history::LogEvent(ok?history::LogKind::Activity:history::LogKind::Failure,ok?"settings":"failed",line);
    if(!ok)MessageBoxW(GetActiveWindow(),failure,L"To Disk",MB_OK|MB_ICONERROR);
}catch(...) {/* Do not unwind allocating settings/UI operations through CEF. */}
static void Outside(Delegate* self,Menu* menu,const void* point) { auto* o=Self(self)->original; if(o && o->outside) o->outside(o,menu,point); else ReleaseMenu(menu); }
static void Open(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->open) o->open(o,menu,rtl); else ReleaseMenu(menu); }
static void Close(Delegate* self,Menu* menu,int rtl) { auto* o=Self(self)->original; if(o && o->close) o->close(o,menu,rtl); else ReleaseMenu(menu); }
static void WillShow(Delegate* self,Menu* menu) try {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuWillShow);
    MenuReference reference(menu);
    auto* wrapped=Self(self);
    RefreshSpeedMenu(menu);
    if(MenuMethods::IndexOf(menu,marked_id)>=0) RefreshMarkedMenu(menu,history::GetSettings().marked_when_downloaded);
    if(wrapped->CapableCached(menu) && MenuMethods::IndexOf(menu,root_id)>=0) wrapped->opening.Show();
    auto* original=wrapped->original;
    // CEF transfers one menu reference into every delegate callback. Retain
    // another across the original callback, which consumes its argument.
    if(original && original->will_show) { menu->base.add_ref(&menu->base); original->will_show(original,menu); }
    if(wrapped->CapableCached(menu) && MenuMethods::IndexOf(menu,song_id)>=0) {
        auto& snapshot=wrapped->opening.snapshot;
        snapshot.RefreshSong(!snapshot.song_bound);
        UpdateQuality(menu,&snapshot.song,!snapshot.song_file.empty());
    } else Populate(menu);
}catch(...) {/* Keep prior labels if allocation fails. */}
static void Closed(Delegate* self,Menu* menu) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuClosed);
    MenuReference reference(menu);
    auto* wrapped=Self(self);
    if(wrapped->CapableCached(menu) && MenuMethods::IndexOf(menu,root_id)>=0) wrapped->opening.Close();
    wrapped->ClearValidation();
    auto* original=wrapped->original;
    if(original && original->closed)original->closed(original,reference.Take());
}
static int Format(Delegate* self,Menu* menu,String* label) {
    history::HookDiagnosticScope diagnostic(history::diagnostics::Phase::MenuFormat);
    MenuReference reference(menu);
    auto* wrapped=Self(self);
    auto* original=wrapped->original;
    int changed=0;
    try {
        if(original && original->format) {
            menu->base.add_ref(&menu->base);
            changed=original->format(original,menu,label);
        }
        if(!label || !label->str)return changed;
        const std::wstring_view text(label->str,label->length);
        auto& snapshot=wrapped->opening.snapshot;
        // CEF builds visible children after formatting the parent label and before WillShow.
        if(text==root_label) {
            if(!wrapped->CapableCached(menu))return changed;
            auto* child=MenuMethods::GetSubmenu(menu,root_id);
            MenuReference child_reference(child);
            RefreshSpeedMenu(child);
            if(wrapped->HasQualityRows(child)) {
                snapshot.RefreshSong(!wrapped->opening.Visible());
                UpdateQuality(child,&snapshot.song,!snapshot.song_file.empty());
                if(!wrapped->opening.Visible()) RefreshRecent(wrapped,child);
            }
            return changed;
        }
        // Skip ordinary labels before probing menu capabilities or scanning rows.
        if(!set_string)return changed;
        for(size_t i=0;i<history::kPlaybackLabelPrefixes.size();++i) {
            const auto prefix=history::kPlaybackLabelPrefixes[i];
            if(text.substr(0,prefix.size())!=prefix)continue;
            if(!wrapped->HasQualityRows(menu))break;
            if(i==0) snapshot.RefreshSong(!snapshot.song_bound);
            const auto row=history::PlaybackQualityLabel(i==0 ? snapshot.song : history::ReadPlaybackQuality(),i);
            if(i==0) MenuMethods::SetEnabled(menu,song_id,!snapshot.song_file.empty());
            if(set_string(row.data(),row.size(),label,1))changed=1;
            break;
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
