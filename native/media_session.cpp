#include "media_session.h"
#include <roapi.h>
#include <winstring.h>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <cwctype>
#include <atomic>
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
static bool Factory(const wchar_t* name, const wchar_t* iid, Com& out) {
    HSTRING text=nullptr; auto id=Guid(iid);
    if(FAILED(WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),&text))) return false;
    HRESULT hr=RoGetActivationFactory(text,id,out.out()); WindowsDeleteString(text);
    return SUCCEEDED(hr);
}
static bool Await(void* operation, Com& result) {
    Com info;
    if(!Query(operation,L"{00000036-0000-0000-c000-000000000046}",info)) return false;
    ULONGLONG end=GetTickCount64()+5000;
    for(;;) {
        int status=0; if(FAILED(Call(info.p,7,&status))) return false;
        if(status==1) return SUCCEEDED(Call(operation,8,result.out()));
        if(status!=0) return false;
        if(GetTickCount64()>=end) { Call(info.p,9); return false; }
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
struct Timeline {
    int64_t start=0,end=0,position=0,updated=0;
    int status=0;
    bool Same(const Timeline& other) const {
        return start==other.start&&end==other.end&&position==other.position&&
               updated==other.updated&&status==other.status;
    }
};
bool ReadTimeline(void* session,Timeline& out) {
    Com timeline,playback;
    return SUCCEEDED(Call(session,8,timeline.out()))&&SUCCEEDED(Call(session,9,playback.out()))&&
        SUCCEEDED(Call(timeline.p,6,&out.start))&&SUCCEEDED(Call(timeline.p,7,&out.end))&&
        SUCCEEDED(Call(timeline.p,10,&out.position))&&SUCCEEDED(Call(timeline.p,11,&out.updated))&&
        SUCCEEDED(Call(playback.p,7,&out.status));
}
bool SpotifySession(void* manager,Com& session,std::wstring& app) {
    Com sessions;
    if(FAILED(Call(manager,7,sessions.out())))return false;
    unsigned count=0;if(FAILED(Call(sessions.p,7,&count))||count>64)return false;
    for(unsigned i=0;i<count;++i) {
        if(FAILED(Call(sessions.p,6,i,session.out())))continue;
        app=String(session.p,6);auto lower=app;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return wchar_t(towlower(c));});
        if(lower.find(L"spotify")!=std::wstring::npos)return true;
        session.out();
    }
    return false;
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
    if(!initialized_) {
        const auto hr=RoInitialize(RO_INIT_MULTITHREADED);
        if(FAILED(hr))return MediaRead::Failed;
        initialized_=true;
    }
    if(!manager_) {
        Com factory,operation,manager;
        if(!Factory(L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
                    L"{2050c4ee-11a0-57de-aed7-c97c70338245}",factory)||
           FAILED(Call(factory.p,6,operation.out()))||!Await(operation.p,manager))return MediaRead::Failed;
        manager_=manager.p;manager.p=nullptr;
    }
    Com session;std::wstring app;Timeline before;
    if(!SpotifySession(manager_,session,app)||!ReadTimeline(session.p,before))return MediaRead::Failed;
    Com operation,properties;
    if(FAILED(Call(session.p,7,operation.out()))||!Await(operation.p,properties))return MediaRead::Failed;
    Media media;media.app=app;
    media.title=String(properties.p,6);media.album_artist=String(properties.p,8);
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
    // A completed property operation must not be paired with a timeline/session
    // that changed while it was outstanding (including equal-duration tracks).
    Com current;std::wstring current_app;Timeline after;
    if(!SpotifySession(manager_,current,current_app)||!ReadTimeline(current.p,after))return MediaRead::Failed;
    if(current_app!=app||!SameSession(session.p,current.p)||!before.Same(after))return MediaRead::Pending;
    const double observed=double(GetTickCount64())/1000.0;
    media.playing=after.status==4;
    media.duration=(double(after.end)-double(after.start))/10000000.0;
    media.position=(double(after.position)-double(after.start))/10000000.0;
    FILETIME now;GetSystemTimeAsFileTime(&now);
    const uint64_t ticks=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    const double elapsed=(double(ticks)-double(after.updated))/10000000.0;
    if(media.playing&&elapsed>=0&&elapsed<86400)media.position+=elapsed;
    media.position=std::max(0.0,std::min(media.position,media.duration));
    if(!BoundedMedia(media))return MediaRead::Failed;
    out=std::move(media);observed_at=observed;return MediaRead::Ready;
}
struct MediaSnapshotReader::State {
    struct Result {MediaRead status=MediaRead::Failed;Media media;double observed=0;uint64_t sequence=0;};
    SRWLOCK lock=SRWLOCK_INIT;
    std::shared_ptr<const Result> result;
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
MediaRead MediaSnapshotReader::TryRead(Media& out,double& observed_at,uint64_t& sequence) noexcept {
    try {
        if(!state_||!state_->running.load(std::memory_order_acquire)||!TryAcquireSRWLockShared(&state_->lock))return MediaRead::Pending;
        const auto result=state_->result;ReleaseSRWLockShared(&state_->lock);
        if(!result||result->sequence==sequence)return MediaRead::Pending;
        const double now=double(GetTickCount64())/1000.0;
        if(result->status==MediaRead::Failed||now-result->observed>3||result->observed>now) {
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
                try {
                    observation=state->fetch?(state->fetch(result->media,result->observed,state->context)?MediaRead::Ready:MediaRead::Failed):reader.Observe(result->media,result->observed);
                }catch(...) {/* Provider/COM failures publish a failed observation. */}
                if(observation==MediaRead::Pending) {
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
                if(!valid){result->media={};result->observed=completed;}
                std::shared_ptr<const State::Result> retired;
                if(state->running.load(std::memory_order_acquire)) {
                    AcquireSRWLockExclusive(&state->lock);
                    if(state->running.load(std::memory_order_acquire)) {
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
}
