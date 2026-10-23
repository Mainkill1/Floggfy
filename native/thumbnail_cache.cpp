#define WIN32_LEAN_AND_MEAN
#include "thumbnail_cache.h"
#include "srw_guard.h"
#include <cmath>
#include <utility>
namespace history {
namespace {
bool ValidIdentity(const Media& media) noexcept {
    constexpr size_t limit=4096;
    return !media.title.empty()&&media.title.size()<=limit&&media.artist.size()<=limit&&
        media.album.size()<=limit&&media.album_artist.size()<=limit&&media.app.size()<=limit&&
        std::isfinite(media.duration)&&media.duration>0&&media.duration<=86400;
}
struct ExclusiveUnlock {SRWLOCK& lock;~ExclusiveUnlock(){ReleaseSRWLockExclusive(&lock);}};
struct SharedUnlock {SRWLOCK& lock;~SharedUnlock(){ReleaseSRWLockShared(&lock);}};
std::shared_ptr<const Media> Identity(const Media& media) {
    auto identity=std::make_shared<Media>();
    identity->title=media.title;identity->artist=media.artist;identity->album=media.album;
    identity->album_artist=media.album_artist;identity->app=media.app;
    identity->duration=media.duration;identity->track=media.track;
    return identity;
}
}
bool SameArtworkIdentity(const Media& first,const Media& second) noexcept {
    return first.title==second.title&&first.artist==second.artist&&first.album==second.album&&
        first.album_artist==second.album_artist&&first.app==second.app&&first.track==second.track&&
        std::isfinite(first.duration)&&std::isfinite(second.duration)&&std::fabs(first.duration-second.duration)<=0.5;
}
WindowsThumbnailCache::WindowsThumbnailCache(Fetch fetch,void* context,Current current,Clock clock) noexcept
    :fetch_(fetch),context_(context),current_check_(current),clock_(clock) {}
ULONGLONG WindowsThumbnailCache::Now() const noexcept {return clock_?clock_(context_):GetTickCount64();}
WindowsThumbnailCache::~WindowsThumbnailCache(){Stop();}
bool WindowsThumbnailCache::Start() noexcept {
    if(thread_.load(std::memory_order_acquire))return true;
    if(!TryAcquireSRWLockExclusive(&start_lock_))return false;
    ExclusiveUnlock unlock{start_lock_};
    if(thread_.load(std::memory_order_acquire))return true;
    event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event_)return false;
    running_.store(true,std::memory_order_release);
    HANDLE thread=CreateThread(nullptr,0,Run,this,0,nullptr);
    if(!thread){running_=false;CloseHandle(event_);event_=nullptr;return false;}
    thread_.store(thread,std::memory_order_release);
    return true;
}
void WindowsThumbnailCache::Request(const Media& media) noexcept {
    try {
        if(!fetch_||!ValidIdentity(media))return;
        const auto identity=Identity(media); // Bounded names, never copy cover bytes.
        if(!Start()||!TryAcquireSRWLockExclusive(&state_lock_))return;
        bool signal=false;
        {
            ExclusiveUnlock unlock{state_lock_};
            if(!current_||!SameArtworkIdentity(*current_,*identity)) {
                current_=identity;++serial_;retry_at_=0;
                pending_.reset();pending_serial_=0;
            }
            const auto now=Now();
            for(const auto& entry:entries_)
                if(entry&&SameArtworkIdentity(*entry->identity,*identity)&&now-entry->updated<30000)return;
            if(now<retry_at_||(pending_&&pending_serial_==serial_)||active_serial_==serial_)return;
            pending_=identity;pending_serial_=serial_;signal=true;
        }
        if(signal)SetEvent(event_);
    }catch(...) {/* Optional artwork must not stop capture or scheduling. */}
}
bool WindowsThumbnailCache::Get(const Media& media,std::vector<uint8_t>& bytes,std::wstring& extension) noexcept {
    try {
        if(!ValidIdentity(media)||!TryAcquireSRWLockShared(&state_lock_))return false;
        std::shared_ptr<const Entry> found;
        {
            SharedUnlock unlock{state_lock_};
            for(const auto& entry:entries_)
                if(entry&&SameArtworkIdentity(*entry->identity,media)){found=entry;break;}
        }
        if(!found)return false;
        auto ready=found->bytes;auto suffix=found->extension;
        bytes.swap(ready);extension.swap(suffix);
        return true;
    }catch(...) {return false;}
}
DWORD WINAPI WindowsThumbnailCache::Run(void* context) {
    static_cast<WindowsThumbnailCache*>(context)->Work();return 0;
}
void WindowsThumbnailCache::Work() noexcept {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    while(running_.load(std::memory_order_acquire)) {
        std::shared_ptr<const Media> request;uint64_t serial=0;
        {
            ExclusiveSrw guard(state_lock_);
            if(pending_){request.swap(pending_);serial=pending_serial_;pending_serial_=0;active_serial_=serial;}
        }
        if(!request){WaitForSingleObject(event_,1000);continue;}
        std::shared_ptr<Entry> ready;
        try {
            Media fetched;
            if((!current_check_||current_check_(*request,context_))&&fetch_(*request,fetched,context_)&&
               ValidIdentity(fetched)&&SameArtworkIdentity(*request,fetched)&&!fetched.cover.empty()&&fetched.cover.size()<=byte_limit&&
               (!current_check_||current_check_(*request,context_))) {
                ImageInfo info;
                if(ReadImageInfo(fetched.cover,info)) {
                    ready=std::make_shared<Entry>();ready->identity=request;
                    ready->bytes=std::move(fetched.cover);ready->extension=info.mime=="image/png"?L".png":L".jpg";
                    ready->area=uint64_t(info.width)*info.height;ready->updated=Now();
                }
            }
        }catch(...) {/* Keep any earlier cover; retry only on a later request. */}
        std::shared_ptr<const Entry> retired;
        {
            ExclusiveSrw guard(state_lock_);
            if(active_serial_==serial)active_serial_=0;
            if(running_.load(std::memory_order_acquire)&&serial==serial_&&current_&&SameArtworkIdentity(*current_,*request)) {
                retry_at_=Now()+(ready?30000:5000);
                if(ready) {
                    size_t slot=entries_.size();uint64_t oldest=UINT64_MAX;
                    for(size_t i=0;i<entries_.size();++i) {
                        if(entries_[i]&&SameArtworkIdentity(*entries_[i]->identity,*request)){slot=i;break;}
                        if(!entries_[i]){slot=i;oldest=0;}
                        else if(entries_[i]->order<oldest){slot=i;oldest=entries_[i]->order;}
                    }
                    if(slot<entries_.size()&&(!entries_[slot]||!SameArtworkIdentity(*entries_[slot]->identity,*request)||ready->area>=entries_[slot]->area)) {
                        ready->order=++order_;retired.swap(entries_[slot]);entries_[slot]=std::move(ready);
                    }
                }
            }
        }
        // Large byte buffers are released outside the state lock.
    }
}
void WindowsThumbnailCache::Stop() noexcept {
    HANDLE thread=thread_.load(std::memory_order_acquire);
    if(!thread)return;
    running_.store(false,std::memory_order_release);SetEvent(event_);
    WaitForSingleObject(thread,INFINITE);CloseHandle(thread);CloseHandle(event_);
    thread_.store(nullptr,std::memory_order_release);event_=nullptr;
}
}
