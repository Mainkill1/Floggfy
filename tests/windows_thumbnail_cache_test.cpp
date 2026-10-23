// One issue-2 gate: deterministic asynchronous storage, without live WinRT or
// client/network dependencies. The production adapter is statically checked.
#include "../native/thumbnail_cache.h"
#include "../native/history_settings.h"
#include "artwork_fixture.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <new>
static thread_local bool fail_allocation=false;
void* operator new(std::size_t n){if(fail_allocation){fail_allocation=false;throw std::bad_alloc();}if(auto p=HeapAlloc(GetProcessHeap(),0,n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
void operator delete(void* p,std::size_t) noexcept {if(p)HeapFree(GetProcessHeap(),0,p);}
namespace {
using history::Media;
using history::WindowsThumbnailCache;
enum class Mode {Normal,Missing,Invalid,Oversize,Throw,WrongIdentity};
struct Source {
    bool metadata_enabled;
    std::atomic<Mode> mode{Mode::Normal};
    std::atomic<unsigned> current{1},calls{0},checks{0},block_track{0},worker_clocks{0},worker_track{0},completed_track{0};
    std::atomic<DWORD> worker_thread{0};
    std::atomic<ULONGLONG> clock{1000};
    std::array<std::atomic<unsigned>,64> seen{};
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    explicit Source(bool metadata):metadata_enabled(metadata){assert(entered&&release);for(auto& track:seen)track.store(0);}
    ~Source(){CloseHandle(entered);CloseHandle(release);}
};
Media Song(unsigned track) {
    Media media;media.title=L"Song "+std::to_wstring(track);media.artist=L"Artist";media.album=L"Album";
    media.album_artist=L"Album Artist";media.app=L"Spotify.exe";media.duration=180;media.track=track;return media;
}
bool Current(const Media& expected,void* context) {
    auto& source=*static_cast<Source*>(context);source.worker_thread=GetCurrentThreadId();source.worker_track=expected.track;++source.checks;return expected.track==source.current.load();
}
ULONGLONG Clock(void* context) noexcept {
    auto& source=*static_cast<Source*>(context);const auto now=source.clock.load();
    if(GetCurrentThreadId()==source.worker_thread.load()){source.completed_track=source.worker_track.load();++source.worker_clocks;}
    return now;
}
bool Fetch(const Media& expected,Media& fetched,void* context) {
    auto& source=*static_cast<Source*>(context);assert(!source.metadata_enabled);
    const auto mode=source.mode.load();const auto call=source.calls.fetch_add(1);
    if(call<source.seen.size())source.seen[call]=expected.track;
    if(source.block_track.load()==expected.track){SetEvent(source.entered);assert(WaitForSingleObject(source.release,10000)==WAIT_OBJECT_0);}
    if(mode==Mode::Throw)throw std::bad_alloc();
    if(mode==Mode::Missing)return false;
    fetched=expected;fetched.cover=expected.track%2?artwork_fixture::png():artwork_fixture::jpeg();
    fetched.cover_extension=L".untrusted";
    if(mode==Mode::Invalid)fetched.cover={1,2,3};
    if(mode==Mode::Oversize)fetched.cover.resize(4*1024*1024+1,0);
    if(mode==Mode::WrongIdentity)fetched.title=L"Late SMTC track";
    return true;
}
template<class Predicate> void Wait(Predicate ready) {
    const auto deadline=GetTickCount64()+5000;
    while(!ready()){assert(GetTickCount64()<deadline);Sleep(1);}
}
std::vector<uint8_t> Ready(WindowsThumbnailCache& cache,const Media& media) {
    std::vector<uint8_t> bytes;std::wstring extension;
    Wait([&]{return cache.Get(media,bytes,extension);});
    assert(extension==(media.track%2?L".png":L".jpg"));return bytes;
}
std::vector<uint8_t> FetchReady(WindowsThumbnailCache& cache,const Media& media) {
    std::vector<uint8_t> bytes;std::wstring extension;
    Wait([&]{cache.Request(media);return cache.Get(media,bytes,extension);});
    assert(extension==(media.track%2?L".png":L".jpg"));return bytes;
}
void Missing(WindowsThumbnailCache& cache,const Media& media) {
    std::vector<uint8_t> bytes{9};std::wstring extension=L"retain";
    assert(!cache.Get(media,bytes,extension)&&bytes==std::vector<uint8_t>{9}&&extension==L"retain");
}
}
int main() {
    history::Settings preferences;preferences.metadata=false; // Metadata=0 must not gate this independent fallback.
    Source source(preferences.metadata);
    WindowsThumbnailCache cache(Fetch,&source,Current,Clock);
    const auto a=Song(1),b=Song(2),c=Song(3);
    Missing(cache,a);assert(FetchReady(cache,a)==artwork_fixture::png());
    auto mismatch=a;mismatch.duration+=1;Missing(cache,mismatch);
    mismatch=a;mismatch.app=L"Other.exe";Missing(cache,mismatch);
    mismatch=a;mismatch.album=L"Other";Missing(cache,mismatch);
    std::vector<uint8_t> unchanged{9};std::wstring suffix=L"retain";
    Wait([&]{
        fail_allocation=true;const bool got=cache.Get(a,unchanged,suffix);const bool injected=!fail_allocation;fail_allocation=false;
        if(!injected)return false;
        assert(!got&&unchanged==std::vector<uint8_t>{9}&&suffix==L"retain");
        return true;
    });
    const auto not_queued=Song(99);fail_allocation=true;cache.Request(not_queued);fail_allocation=false;
    assert(Ready(cache,a)==artwork_fixture::png());
    // Fetching B is blocked, but capture-style requests and retained A reads
    // must stay short. The single pending slot coalesces all intermediate work.
    source.current=2;source.block_track=2;cache.Request(b);
    Wait([&]{cache.Request(b);return WaitForSingleObject(source.entered,0)==WAIT_OBJECT_0;});
    const auto start=GetTickCount64();
    for(unsigned i=100;i<103;++i){source.current=i;cache.Request(Song(i));assert(Ready(cache,a)==artwork_fixture::png());}
    source.current=3;cache.Request(c);assert(GetTickCount64()-start<1000);
    SetEvent(source.release);source.block_track=0;
    assert(Ready(cache,c)==artwork_fixture::png());Missing(cache,b);assert(Ready(cache,a)==artwork_fixture::png());
    assert(source.calls.load()==3); // A, in-flight B, latest C; none of the coalesced identities fetched.
    for(unsigned i=0;i<source.calls.load();++i)assert(source.seen[i].load()<100);
    unsigned track=5;
    for(const auto mode:{Mode::Missing,Mode::Invalid,Mode::Oversize,Mode::Throw,Mode::WrongIdentity}) {
        const auto media=Song(track++);source.current=media.track;source.mode=mode;
        const auto before=source.calls.load(),before_clock=source.worker_clocks.load();
        cache.Request(media);Wait([&]{cache.Request(media);return source.calls.load()>before&&source.worker_clocks.load()>before_clock&&source.completed_track.load()==media.track;});
        // Advancing the injected clock permits exactly one retry. If an invalid
        // result were cached as a fresh cover, the retry would remain suppressed.
        source.clock.fetch_add(6000);
        Wait([&]{cache.Request(media);return source.calls.load()>=before+2;});Missing(cache,media);
    }
    // A fresh client identity rejects a lagging SMTC request before any fetch.
    const auto stale=Song(12);source.current=99;source.mode=Mode::Normal;
    const auto before_calls=source.calls.load(),before_checks=source.checks.load(),before_clock=source.worker_clocks.load();cache.Request(stale);
    Wait([&]{cache.Request(stale);return source.checks.load()>before_checks&&source.worker_clocks.load()>before_clock&&source.completed_track.load()==stale.track;});Missing(cache,stale);assert(source.calls.load()==before_calls);
    source.current=12;source.clock.fetch_add(6000);
    Wait([&]{cache.Request(stale);std::vector<uint8_t> bytes;std::wstring extension;return cache.Get(stale,bytes,extension);});
    for(unsigned i=20;i<30;++i){source.current=i;const auto media=Song(i);cache.Request(media);FetchReady(cache,media);}
    Missing(cache,a);Missing(cache,Song(21));
    for(unsigned i=22;i<30;++i)Ready(cache,Song(i));
    cache.Stop();
    puts("PASS: independent Metadata=0 thumbnails retain A across B, bound/coalesce work and storage, reject stale identities and failures without blocking capture-style callers");
}
