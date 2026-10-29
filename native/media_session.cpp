#include "media_session.h"
#include "media_timeline.h"
#include <roapi.h>
#include <winstring.h>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <cwctype>
#include <atomic>
#include <array>
#include <cmath>
#include <new>
namespace history {
// These slots are the public WinRT COM ABI, copied from Windows SDK 26100.
// No Spotify UI internals, generated projections or runtime helpers are used.
template<class... A> static HRESULT Call(void* p, unsigned slot, A... args) {
    if(!p) return E_POINTER;
    auto f=reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,A...)>((*static_cast<void***>(p))[slot]);
    return f(p,args...);
}
static void Release(void* p) { if(p) static_cast<IUnknown*>(p)->Release(); }
struct Com {
    void* p=nullptr;
    ~Com() { Release(p); }
    void** out() { Release(p); p=nullptr; return &p; }
};
static GUID Guid(const wchar_t* text) { GUID g={}; CLSIDFromString(text,&g); return g; }
static bool Query(void* p, const wchar_t* iid, Com& out) {
    auto id=Guid(iid); return SUCCEEDED(Call(p,0,&id,out.out()));
}
static HRESULT Factory(const wchar_t* name, const wchar_t* iid, Com& out) {
    HSTRING text=nullptr; auto id=Guid(iid);
    HRESULT created=WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&text);
    if(FAILED(created))return created;
    HRESULT hr=RoGetActivationFactory(text,id,out.out()); WindowsDeleteString(text);
    return hr;
}
static bool Check(HRESULT hr,MediaDiagnostic& diagnostic,const char* stage) {
    if(SUCCEEDED(hr))return true;
    diagnostic.stage=stage;diagnostic.reason="api-error";diagnostic.hresult=hr;return false;
}
static bool Await(void* operation,Com& result,MediaDiagnostic& diagnostic,const char* stage) {
    diagnostic.stage=stage;
    Com info;
    auto id=Guid(L"{00000036-0000-0000-c000-000000000046}");
    if(!Check(Call(operation,0,&id,info.out()),diagnostic,stage))return false;
    ULONGLONG end=GetTickCount64()+5000;
    for(;;) {
        int status=0;
        if(!Check(Call(info.p,7,&status),diagnostic,stage))return false;
        if(status==1)return Check(Call(operation,8,result.out()),diagnostic,stage);
        if(status!=0) {
            HRESULT error=E_ABORT;
            if(status==3 && !Check(Call(info.p,8,&error),diagnostic,stage))return false;
            diagnostic.reason=status==2?"async-cancelled":"async-error";
            diagnostic.hresult=error;return false;
        }
        if(GetTickCount64()>=end) {
            Call(info.p,9);diagnostic.reason="async-timeout";
            diagnostic.hresult=HRESULT_FROM_WIN32(ERROR_TIMEOUT);return false;
        }
        Sleep(5);
    }
}
struct OwnedString {
    HSTRING value=nullptr;
    ~OwnedString(){WindowsDeleteString(value);}
};
static std::wstring String(void* object,unsigned slot) {
    OwnedString text;if(FAILED(Call(object,slot,&text.value)))return {};
    UINT32 size=0;const auto data=WindowsGetStringRawBuffer(text.value,&size);
    return size<=4096?std::wstring(data,size):std::wstring{};
}
std::string Utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::string Json(const std::string& s) {
    const char* hex="0123456789abcdef"; std::string out="\"";
    for(unsigned char c:s) {
        if(c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
std::string Media::Key() const {
    return Utf8(title)+"\x1f"+Utf8(artist)+"\x1f"+Utf8(album);
}
namespace {
using Timeline=media_detail::MediaTimeline;
bool ReadTimeline(void* session,Timeline& out,MediaDiagnostic& diagnostic) {
    Com timeline,playback;
    if(!Check(Call(session,8,timeline.out()),diagnostic,"timeline-properties") ||
       !Check(Call(session,9,playback.out()),diagnostic,"playback-info") ||
       !Check(Call(timeline.p,6,&out.start),diagnostic,"timeline-start") ||
       !Check(Call(timeline.p,7,&out.end),diagnostic,"timeline-end") ||
       !Check(Call(timeline.p,10,&out.position),diagnostic,"timeline-position") ||
       !Check(Call(timeline.p,11,&out.updated),diagnostic,"timeline-updated") ||
       !Check(Call(playback.p,7,&out.status),diagnostic,"playback-status"))return false;
    diagnostic.position=(double(out.position)-double(out.start))/10000000.0;
    diagnostic.duration=(double(out.end)-double(out.start))/10000000.0;
    diagnostic.status=out.status;
    if(out.Valid())return true;
    diagnostic.stage="timeline-validation";diagnostic.hresult=0;
    diagnostic.reason=out.end<=out.start?"duration-unavailable":out.updated<=0?"timestamp-unavailable":"invalid-bounds";
    return false;
}
bool SpotifySession(void* manager,Com& session,std::wstring& app,Timeline& timeline,MediaDiagnostic& diagnostic) {
    Com sessions;
    if(!Check(Call(manager,7,sessions.out()),diagnostic,"enumerate-sessions"))return false;
    unsigned count=0;
    if(!Check(Call(sessions.p,7,&count),diagnostic,"session-count"))return false;
    diagnostic.sessions=count;diagnostic.candidates=0;
    if(count>64){diagnostic.stage="session-count";diagnostic.reason="session-limit";return false;}
    diagnostic.stage="select-session";diagnostic.reason="no-spotify-session";diagnostic.hresult=0;
    for(unsigned i=0;i<count;++i) {
        if(!Check(Call(sessions.p,6,i,session.out()),diagnostic,"session-item"))continue;
        OwnedString text;
        if(!Check(Call(session.p,6,&text.value),diagnostic,"session-app-id"))continue;
        UINT32 size=0;const auto* raw=WindowsGetStringRawBuffer(text.value,&size);
        if(size>4096)continue;
        app.assign(raw,size);auto lower=app;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return wchar_t(towlower(c));});
        if(lower.find(L"spotify")==std::wstring::npos)continue;
        ++diagnostic.candidates;
        // A stale/empty first session must not mask another usable session.
        if(ReadTimeline(session.p,timeline,diagnostic))return true;
    }
    session.out();return false;
}
bool SameSession(void* first,void* second) {
    Com a,b;
    return Query(first,L"{00000000-0000-0000-C000-000000000046}",a)&&
           Query(second,L"{00000000-0000-0000-C000-000000000046}",b)&&a.p==b.p;
}
bool BoundedMedia(const Media& media) {
    return !media.title.empty()&&media.title.size()<=4096&&media.artist.size()<=4096&&
        media.album.size()<=4096&&media.album_artist.size()<=4096&&media.app.size()<=4096&&
        media.genre.size()<=10280&&std::isfinite(media.position)&&std::isfinite(media.duration)&&
        media.duration>0&&media.duration<=86400&&media.position>=0&&media.position<=media.duration;
}
}
namespace {
struct ClientSnapshot {
    Media media;double observed=0;uint64_t sequence=0;
    ClientMediaRead status=ClientMediaRead::Unavailable;
    const char* reason="client-not-discovered";
};
SRWLOCK client_media_lock=SRWLOCK_INIT;
std::shared_ptr<const ClientSnapshot> client_media;
uint64_t client_media_sequence=0;
std::array<std::shared_ptr<const ClientSnapshot>,32> client_pending;
size_t client_head=0,client_count=0;
std::atomic<bool> client_media_lost{false};
void StoreClientSnapshot(std::shared_ptr<ClientSnapshot> next) {
    std::array<std::shared_ptr<const ClientSnapshot>,32> retired;
    if(!TryAcquireSRWLockExclusive(&client_media_lock)){client_media_lost.store(true,std::memory_order_release);return;}
    if(client_count==client_pending.size()) {
        // Never silently lose a short buffering/remote/transition event.
        retired.swap(client_pending);client_head=client_count=0;
        next->status=ClientMediaRead::Blocked;next->reason="client-observation-overflow";
    }
    next->sequence=++client_media_sequence;
    client_pending[(client_head+client_count)%client_pending.size()]=next;++client_count;
    std::shared_ptr<const ClientSnapshot> previous;previous.swap(client_media);client_media=std::move(next);
    ReleaseSRWLockExclusive(&client_media_lock);
}
}
void InvalidateClientMedia() noexcept {client_media_lost.store(true,std::memory_order_release);}
void PublishClientMediaUnavailable(const char* reason,bool blocked) {
    auto next=std::make_shared<ClientSnapshot>();next->observed=double(GetTickCount64())/1000;
    next->reason=reason;next->status=blocked?ClientMediaRead::Blocked:ClientMediaRead::Unavailable;
    StoreClientSnapshot(std::move(next));
}
bool PublishClientMedia(Media media,double sampled_utc_ms) {
    FILETIME now;GetSystemTimeAsFileTime(&now);
    const auto ticks=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    // Chromium and coarse FILETIME samples can differ by a clock tick. Bound
    // skew to 250 ms; never move the monotonic observation into the future.
    const double age=(double(ticks)-116444736000000000.0)/10000.0-sampled_utc_ms;
    if(!std::isfinite(sampled_utc_ms)||age< -250||age>3000||!BoundedMedia(media)||media.spotify_uri.empty()||media.spotify_uri.size()>64) {
        PublishClientMediaUnavailable("client-stale-or-invalid");return false;
    }
    auto next=std::make_shared<ClientSnapshot>();media.source=MediaSource::Spotify;
    next->media=std::move(media);next->observed=double(GetTickCount64())/1000-std::max(0.0,age)/1000;
    next->status=ClientMediaRead::Ready;next->reason="client-timeline-ready";
    StoreClientSnapshot(std::move(next));return true;
}
void PublishClientBufferingTransition(Media media,double sampled_utc_ms,double changed_utc_ms) {
    FILETIME now;GetSystemTimeAsFileTime(&now);
    const auto ticks=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    const double utc=(double(ticks)-116444736000000000.0)/10000.0;
    if(!std::isfinite(sampled_utc_ms)||!std::isfinite(changed_utc_ms)||utc-sampled_utc_ms< -250||utc-sampled_utc_ms>3000||
       utc-changed_utc_ms< -250||utc-changed_utc_ms>3000) {
        PublishClientMediaUnavailable("client-buffering",true);return;
    }
    auto next=std::make_shared<ClientSnapshot>();media.source=MediaSource::Spotify;next->media=std::move(media);
    next->observed=double(GetTickCount64())/1000-std::max(0.0,utc-changed_utc_ms)/1000;
    next->status=ClientMediaRead::Transition;next->reason="client-buffering-transition";
    StoreClientSnapshot(std::move(next));
}
ClientMediaRead TryReadClientMedia(Media& out,double& observed,uint64_t& sequence,MediaDiagnostic& diagnostic) noexcept {
    try {
        diagnostic={};diagnostic.source="spotify";
        std::array<std::shared_ptr<const ClientSnapshot>,32> retired;
        if(!TryAcquireSRWLockExclusive(&client_media_lock))return ClientMediaRead::Pending;
        if(client_media_lost.exchange(false,std::memory_order_acq_rel)) {
            retired.swap(client_pending);client_head=client_count=0;sequence=client_media_sequence;
            ReleaseSRWLockExclusive(&client_media_lock);
            observed=double(GetTickCount64())/1000;diagnostic.stage="client-snapshot";diagnostic.reason="client-observation-loss";diagnostic.client_reason=diagnostic.reason;
            return ClientMediaRead::Blocked;
        }
        auto value=client_media;
        if(client_count) {
            value=std::move(client_pending[client_head]);client_head=(client_head+1)%client_pending.size();--client_count;
        }
        ReleaseSRWLockExclusive(&client_media_lock);
        if(!value){diagnostic.stage="client-snapshot";diagnostic.reason="client-not-discovered";return ClientMediaRead::Unavailable;}
        const auto now=double(GetTickCount64())/1000;
        diagnostic.stage="client-snapshot";diagnostic.reason=value->reason;diagnostic.client_reason=value->reason;
        if(now-value->observed>3||value->observed>now){diagnostic.reason="client-stale";return ClientMediaRead::Unavailable;}
        if(value->status==ClientMediaRead::Transition) {
            if(sequence==value->sequence)return ClientMediaRead::Pending;
            out=value->media;observed=value->observed;sequence=value->sequence;return ClientMediaRead::Transition;
        }
        if(value->status!=ClientMediaRead::Ready){observed=value->observed;sequence=value->sequence;return value->status;}
        diagnostic.stage="ready";diagnostic.position=value->media.position;diagnostic.duration=value->media.duration;
        diagnostic.title_present=true;diagnostic.status=value->media.playing?4:5;
        if(sequence==value->sequence)return ClientMediaRead::Pending;
        auto media=value->media;out=std::move(media);observed=value->observed;sequence=value->sequence;return ClientMediaRead::Ready;
    }catch(...){diagnostic.stage="client-snapshot";diagnostic.reason="client-read-exception";return ClientMediaRead::Unavailable;}
}
MediaReader::~MediaReader() {
    Release(manager_);if(initialized_)RoUninitialize();
}
bool MediaReader::Read(Media& out) {
    double observed=0;return Read(out,observed);
}
bool MediaReader::Read(Media& out,double& observed_at) {
    return Observe(out,observed_at)==MediaRead::Ready;
}
MediaRead MediaReader::Observe(Media& out,double& observed_at) {
    diagnostic_={};
    // WinRT objects can be disconnected after shell/media-service restarts.
    // Release only dead managers; normal missing sessions retain the manager.
    struct RecoverManager {
        void*& manager;MediaDiagnostic& diagnostic;
        ~RecoverManager() {
            const auto hr=uint32_t(diagnostic.hresult);
            if(hr==0x80010108u || hr==0x800706bau || hr==0x80000013u) {
                Release(manager);manager=nullptr;
            }
        }
    } recovery{manager_,diagnostic_};
    if(!initialized_) {
        if(!Check(RoInitialize(RO_INIT_MULTITHREADED),diagnostic_,"initialize-winrt"))return MediaRead::Failed;
        initialized_=true;
    }
    if(!manager_) {
        Com factory,operation,manager;
        if(!Check(Factory(L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
                         L"{2050c4ee-11a0-57de-aed7-c97c70338245}",factory),diagnostic_,"activate-manager") ||
           !Check(Call(factory.p,6,operation.out()),diagnostic_,"request-manager") ||
           !Await(operation.p,manager,diagnostic_,"await-manager"))return MediaRead::Failed;
        manager_=manager.p;manager.p=nullptr;
    }
    Com session;std::wstring app;Timeline before;
    if(!SpotifySession(manager_,session,app,before,diagnostic_))return MediaRead::Failed;
    Com operation,properties;
    if(!Check(Call(session.p,7,operation.out()),diagnostic_,"request-media-properties") ||
       !Await(operation.p,properties,diagnostic_,"await-media-properties"))return MediaRead::Failed;
    Media media;media.app=app;
    OwnedString title;
    if(!Check(Call(properties.p,6,&title.value),diagnostic_,"media-title"))return MediaRead::Failed;
    UINT32 title_size=0;const auto* title_text=WindowsGetStringRawBuffer(title.value,&title_size);
    if(title_size<=4096)media.title.assign(title_text,title_size);
    diagnostic_.title_present=!media.title.empty();
    media.album_artist=String(properties.p,8);
    media.artist=String(properties.p,9);media.album=String(properties.p,10);
    Com genres;
    if(SUCCEEDED(Call(properties.p,12,genres.out()))&&genres.p) {
        unsigned count=0;
        if(SUCCEEDED(Call(genres.p,7,&count))&&count<=20)for(unsigned i=0;i<count;++i) {
            OwnedString value;
            if(SUCCEEDED(Call(genres.p,6,i,&value.value))) {
                UINT32 length=0;const auto* text=WindowsGetStringRawBuffer(value.value,&length);
                if(length&&length<=512){if(!media.genre.empty())media.genre+=L"; ";media.genre.append(text,length);}
            }
        }
    }
    int track=0;Call(properties.p,11,&track);media.track=track>0?unsigned(track):0;
    // Accept ordinary Position/LastUpdatedTime refreshes during the async call.
    // Session changes and discontinuous movement still require a fresh read.
    Com current;std::wstring current_app;Timeline after;
    if(!SpotifySession(manager_,current,current_app,after,diagnostic_))return MediaRead::Failed;
    if(current_app!=app||!SameSession(session.p,current.p)||!before.ContinuesTo(after)) {
        diagnostic_.stage="snapshot-consistency";diagnostic_.reason="session-or-timeline-changed";diagnostic_.hresult=0;
        return MediaRead::Pending;
    }
    const double observed=double(GetTickCount64())/1000.0;
    media.playing=after.status==4;
    media.playback_status=after.status;
    media.duration=(double(after.end)-double(after.start))/10000000.0;
    media.position=(double(after.position)-double(after.start))/10000000.0;
    FILETIME now;GetSystemTimeAsFileTime(&now);
    const uint64_t ticks=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    const double elapsed=(double(ticks)-double(after.updated))/10000000.0;
    if(media.playing&&elapsed>=0&&elapsed<86400)media.position+=elapsed;
    media.position=std::max(0.0,std::min(media.position,media.duration));
    diagnostic_.position=media.position;diagnostic_.duration=media.duration;
    if(!BoundedMedia(media)){diagnostic_.stage="media-validation";diagnostic_.reason=media.title.empty()?"title-unavailable":"invalid-media";diagnostic_.hresult=0;return MediaRead::Failed;}
    diagnostic_.stage="ready";diagnostic_.reason="ok";diagnostic_.hresult=0;
    out=std::move(media);observed_at=observed;return MediaRead::Ready;
}
struct MediaSnapshotReader::State {
    struct Result {MediaRead status=MediaRead::Failed;Media media;double observed=0;uint64_t sequence=0;};
    SRWLOCK lock=SRWLOCK_INIT;
    std::shared_ptr<const Result> result;
    MediaDiagnostic diagnostic;
    std::atomic<bool> running{true},finished{false};
    std::atomic<uint64_t> sequence{0};
    HANDLE wake=nullptr;
    Fetch fetch=nullptr;void* context=nullptr;
    ~State(){if(wake)CloseHandle(wake);}
};
MediaSnapshotReader::MediaSnapshotReader(Fetch fetch,void* context) noexcept:fetch_(fetch),context_(context) {}
MediaSnapshotReader::~MediaSnapshotReader(){Stop();}
bool MediaSnapshotReader::Start() noexcept {
    try {
        uint64_t previous=0;
        if(state_) {
            if(!state_->finished.load(std::memory_order_acquire))return state_->running.load(std::memory_order_acquire);
            previous=state_->sequence.load(std::memory_order_acquire);
        }
        auto next=std::make_shared<State>();next->fetch=fetch_;next->context=context_;next->sequence=previous;
        next->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!next->wake)return false;
        auto* argument=new std::shared_ptr<State>(next);
        HANDLE thread=CreateThread(nullptr,0,Run,argument,0,nullptr);
        if(!thread){delete argument;return false;}
        CloseHandle(thread);state_=std::move(next);return true;
    }catch(...){return false;}
}
void MediaSnapshotReader::Stop() noexcept {
    if(state_){state_->running.store(false,std::memory_order_release);SetEvent(state_->wake);}
}
bool MediaSnapshotReader::IsStopped() const noexcept {
    return !state_||state_->finished.load(std::memory_order_acquire);
}
MediaRead MediaSnapshotReader::TryRead(Media& out,double& observed_at,uint64_t& sequence,MediaDiagnostic* diagnostic) noexcept {
    try {
        if(!state_||!state_->running.load(std::memory_order_acquire)||!TryAcquireSRWLockShared(&state_->lock))return MediaRead::Pending;
        const auto result=state_->result;
        if(diagnostic)*diagnostic=state_->diagnostic;
        ReleaseSRWLockShared(&state_->lock);
        if(!result||result->sequence==sequence)return MediaRead::Pending;
        const double now=double(GetTickCount64())/1000.0;
        if(result->status==MediaRead::Failed||now-result->observed>3||result->observed>now) {
            if(diagnostic && result->status!=MediaRead::Failed){diagnostic->stage="snapshot-freshness";diagnostic->reason="stale-snapshot";}
            observed_at=result->observed;sequence=result->sequence;return MediaRead::Failed;
        }
        auto ready=result->media;out=std::move(ready);observed_at=result->observed;sequence=result->sequence;
        return MediaRead::Ready;
    }catch(...){return MediaRead::Failed;}
}
DWORD WINAPI MediaSnapshotReader::Run(void* argument) {
    std::unique_ptr<std::shared_ptr<State>> owner(static_cast<std::shared_ptr<State>*>(argument));
    const auto state=*owner;owner.reset();
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    {
        MediaReader reader;
        while(state->running.load(std::memory_order_acquire)) {
            DWORD retry_delay=500;
            try {
                auto result=std::make_shared<State::Result>();
                MediaRead observation=MediaRead::Failed;
                MediaDiagnostic diagnostic;
                try {
                    observation=state->fetch?(state->fetch(result->media,result->observed,state->context)?MediaRead::Ready:MediaRead::Failed):reader.Observe(result->media,result->observed);
                    diagnostic=reader.Diagnostic();
                    if(state->fetch){diagnostic.stage=observation==MediaRead::Ready?"ready":"provider";diagnostic.reason=observation==MediaRead::Ready?"ok":"provider-failed";}
                }catch(...) {diagnostic.stage="observer";diagnostic.reason="exception";}
                if(observation==MediaRead::Pending) {
                    {AcquireSRWLockExclusive(&state->lock);state->diagnostic=diagnostic;ReleaseSRWLockExclusive(&state->lock);}
                    // Normal track/timeline movement is not an explicit read
                    // failure. Retry promptly; the consumer still enforces its
                    // unchanged three-second gap rule while nothing is fresh.
                    if(state->running.load(std::memory_order_acquire))WaitForSingleObject(state->wake,50);
                    continue;
                }
                const bool ready=observation==MediaRead::Ready;
                const double completed=double(GetTickCount64())/1000.0;
                const bool valid=ready&&BoundedMedia(result->media)&&std::isfinite(result->observed)&&result->observed>0&&result->observed<=completed;
                result->status=valid?MediaRead::Ready:MediaRead::Failed;
                if(!valid){
                    if(ready){diagnostic.stage="snapshot-validation";diagnostic.reason="invalid-provider-media";}
                    result->media={};result->observed=completed;
                }
                std::shared_ptr<const State::Result> retired;
                if(state->running.load(std::memory_order_acquire)) {
                    AcquireSRWLockExclusive(&state->lock);
                    if(state->running.load(std::memory_order_acquire)) {
                        state->diagnostic=diagnostic;
                        result->sequence=state->sequence.fetch_add(1,std::memory_order_acq_rel)+1;
                        retired.swap(state->result);state->result=std::move(result);
                    }
                    ReleaseSRWLockExclusive(&state->lock);
                }
                // Retired strings and any failed allocation are outside locks.
            }catch(...){/* An unavailable observer leaves a detectable >3s gap. */}
            if(state->running.load(std::memory_order_acquire))WaitForSingleObject(state->wake,retry_delay);
        }
    }
    state->finished.store(true,std::memory_order_release);return 0;
}
MediaRead PlaybackSnapshotReader::TryRead(Media& out,double& observed,uint64_t& sequence,MediaDiagnostic* diagnostic) noexcept {
    if(!running_)return MediaRead::Pending;
    MediaDiagnostic detail;
    const auto client=TryReadClientMedia(out,observed,client_sequence_,detail);
    auto result=MediaRead::Pending;
    if(client==ClientMediaRead::Ready)result=MediaRead::Ready;
    else if(client==ClientMediaRead::Blocked)result=MediaRead::Failed;
    else if(client==ClientMediaRead::Transition)result=MediaRead::Transition;
    else if(client==ClientMediaRead::Unavailable) {
        const char* client_reason=detail.reason;
        detail={};
        if(!windows_started_)windows_started_=windows_.Start();
        if(windows_started_)result=windows_.TryRead(out,observed,windows_sequence_,&detail);
        else {detail.stage="observer-start";detail.reason="thread-start-failed";observed=double(GetTickCount64())/1000;result=MediaRead::Failed;}
        detail.client_reason=client_reason;
    }
    if(diagnostic)*diagnostic=detail;
    if(result!=MediaRead::Pending)sequence=++sequence_;
    return result;
}

}
