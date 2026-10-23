#define WIN32_LEAN_AND_MEAN
#include "song_destination.h"
#include "history_settings.h"
#include "existing_quality.h"
#include <windows.h>
#include <atomic>
#include <optional>
#include <utility>
#include <cstring>
namespace history {
namespace {
struct DestinationRequest {
    std::uint64_t identity=0,serial=0,intent=0,revision=0;
    unsigned generation=0;
    Catalog catalog;
    std::wstring flac,ogg;
};
SRWLOCK destination_lock=SRWLOCK_INIT;
INIT_ONCE destination_once=INIT_ONCE_STATIC_INIT;
std::atomic<bool> destination_enabled{false},refresh_pending{false};
HANDLE destination_event=nullptr;
std::optional<DestinationRequest> latest_request,pending_request;
std::atomic<std::uint64_t> request_serial{0},intent_key{0},cache_revision{0};
std::uint64_t result_serial=0,result_intent=0,active_serial=0;
std::uint64_t destination_identity=0;
unsigned destination_generation=0;
ULONGLONG destination_updated=0;
std::wstring destination_file;
bool probing=false;
struct DestinationExclusive {
    DestinationExclusive(){AcquireSRWLockExclusive(&destination_lock);}
    ~DestinationExclusive(){ReleaseSRWLockExclusive(&destination_lock);}
};
struct DestinationTryExclusive {
    bool locked=TryAcquireSRWLockExclusive(&destination_lock)!=0;
    ~DestinationTryExclusive(){if(locked)ReleaseSRWLockExclusive(&destination_lock);}
};
static bool SameRequest(const DestinationRequest& a,const DestinationRequest& b) {
    const auto& x=a.catalog;const auto& y=b.catalog;
    return a.identity==b.identity && a.generation==b.generation && a.flac==b.flac && a.ogg==b.ogg &&
        x.kind==y.kind && x.artist==y.artist && x.album==y.album && x.title==y.title &&
        x.author==y.author && x.series==y.series && x.book==y.book && x.show==y.show &&
        x.track==y.track && x.episode_year==y.episode_year && x.disc==y.disc &&
        x.spotify_uri==y.spotify_uri && x.duration==y.duration;
}
static std::uint64_t IntentKey(std::uint64_t identity,const std::wstring& root,const Catalog& catalog,
                               bool music_folder,unsigned generation) {
    std::uint64_t hash=14695981039346656037ULL;
    auto number=[&](std::uint64_t value) {for(unsigned i=0;i<8;++i){hash^=uint8_t(value>>(i*8));hash*=1099511628211ULL;}};
    auto wide=[&](const std::wstring& value) {number(value.size());for(wchar_t c:value)number(unsigned(c));};
    number(identity);number(generation);number(music_folder);number(unsigned(catalog.kind));wide(root);
    for(const auto* value:{&catalog.artist,&catalog.album,&catalog.title,&catalog.author,&catalog.series,&catalog.book,&catalog.show})wide(*value);
    number(catalog.track);number(catalog.episode_year);number(catalog.disc);
    number(catalog.spotify_uri.size());for(unsigned char c:catalog.spotify_uri)number(c);
    std::uint64_t duration=0;static_assert(sizeof(duration)==sizeof(catalog.duration),"duration bits");
    memcpy(&duration,&catalog.duration,sizeof(duration));number(duration);
    return hash ? hash : 1;
}
static void InvalidateResult() {
    destination_file.clear();destination_identity=0;destination_updated=0;result_serial=0;result_intent=0;
}
struct FileSignature {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    bool Read(const std::wstring& path) {
        return GetFileAttributesExW(WindowsPath(path).c_str(),GetFileExInfoStandard,&data) &&
               !(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    }
    bool operator==(const FileSignature& other) const {
        const auto& a=data;const auto& b=other.data;
        return a.dwFileAttributes==b.dwFileAttributes && a.nFileSizeHigh==b.nFileSizeHigh && a.nFileSizeLow==b.nFileSizeLow &&
            CompareFileTime(&a.ftLastWriteTime,&b.ftLastWriteTime)==0 && CompareFileTime(&a.ftCreationTime,&b.ftCreationTime)==0;
    }
};
struct VerificationCache {
    std::optional<DestinationRequest> request;
    std::wstring file;
    FileSignature signature;
};
static std::wstring ProbeSavedSong(const DestinationRequest& request,VerificationCache& cache) {
    FileSignature current;
    if(cache.request && cache.request->revision==request.revision && SameRequest(request,*cache.request) &&
       !cache.file.empty() && current.Read(cache.file) && current==cache.signature) return cache.file;
    cache.request.reset();cache.file.clear();
    for(const auto* base:{&request.flac,&request.ogg}) {
        if(!destination_enabled.load(std::memory_order_acquire) || request.serial!=request_serial.load(std::memory_order_acquire))return {};
        const auto file=ResolveRecordingPath(*base,request.catalog);
        if(!destination_enabled.load(std::memory_order_acquire) || request.serial!=request_serial.load(std::memory_order_acquire))return {};
        FileSignature before,after;Quality quality;
        if(!file.empty() && before.Read(file) && ExistingRecording(file,request.catalog,quality) &&
           after.Read(file) && before==after) {
            cache.request=request;cache.file=file;cache.signature=after;return file;
        }
    }
    return {};
}
static DWORD WINAPI DestinationWorker(void*) {
    VerificationCache cache;
    for(;;) {
        WaitForSingleObject(destination_event,INFINITE);
        for(;;) {
            std::optional<DestinationRequest> request;
            {
                DestinationExclusive guard;
                if(!destination_enabled.load(std::memory_order_relaxed)) break;
                if(refresh_pending.exchange(false,std::memory_order_acq_rel) && latest_request &&
                   latest_request->intent==intent_key.load(std::memory_order_acquire)) {
                    try {
                        latest_request->serial=request_serial.load(std::memory_order_acquire);
                        latest_request->revision=cache_revision.load(std::memory_order_acquire);
                        pending_request=*latest_request;
                    }catch(...) {/* A future heartbeat retries an allocation failure. */}
                }
                if(!pending_request) break;
                request=std::move(pending_request);pending_request.reset();
                probing=true;active_serial=request->serial;
            }
            std::wstring file;
            try {file=ProbeSavedSong(*request,cache);}catch(...) {/* A failed probe disables its target. */}
            {
                DestinationExclusive guard;
                probing=false;
                if(destination_enabled.load(std::memory_order_relaxed) && latest_request &&
                   request->serial==request_serial.load(std::memory_order_acquire) &&
                   request->intent==intent_key.load(std::memory_order_acquire) && SameRequest(*request,*latest_request)) {
                    destination_file.swap(file);destination_identity=request->identity;
                    destination_generation=request->generation;destination_updated=GetTickCount64();
                    result_serial=request->serial;result_intent=request->intent;
                }
            }
        }
    }
}
static BOOL CALLBACK CreateDestinationWorker(PINIT_ONCE,PVOID,PVOID*) {
    destination_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!destination_event) return FALSE;
    HANDLE worker=CreateThread(nullptr,0,DestinationWorker,nullptr,0,nullptr);
    if(!worker) {CloseHandle(destination_event);destination_event=nullptr;return FALSE;}
    SetThreadPriority(worker,THREAD_PRIORITY_BELOW_NORMAL);CloseHandle(worker);return TRUE;
}
}
void StartSongDestinationWorker(bool enabled) noexcept {
    if(enabled && !InitOnceExecuteOnce(&destination_once,CreateDestinationWorker,nullptr,nullptr)) enabled=false;
    DestinationExclusive guard;
    destination_enabled.store(enabled,std::memory_order_release);
    if(!enabled) {
        ++request_serial;++cache_revision;refresh_pending.store(false,std::memory_order_release);intent_key.store(0,std::memory_order_release);latest_request.reset();pending_request.reset();InvalidateResult();
    }
}
void PublishSongDestination(std::uint64_t identity,const std::wstring& root,
                            const Catalog& catalog,bool music_folder,unsigned settings_generation) noexcept {
    if(!destination_enabled.load(std::memory_order_acquire)) return;
    // New intent invalidates an old reply even when submission loses a try-lock.
    // Identical requests share an in-flight probe instead of starving slow I/O.
    const auto intent=IntentKey(identity,root,catalog,music_folder,settings_generation);
    if(intent_key.exchange(intent,std::memory_order_acq_rel)!=intent)
        request_serial.fetch_add(1,std::memory_order_acq_rel);
    const auto serial=request_serial.load(std::memory_order_acquire);
    try {
        DestinationRequest request;
        request.identity=identity;request.generation=settings_generation;request.catalog=catalog;
        request.serial=serial;request.intent=intent;request.revision=cache_revision.load(std::memory_order_acquire);
        if(identity && !root.empty()) {
            request.flac=OutputPath(root,catalog,L".flac",music_folder);
            request.ogg=OutputPath(root,catalog,L".ogg",music_folder);
        }
        DestinationTryExclusive guard;
        if(!guard.locked || !destination_enabled.load(std::memory_order_relaxed) ||
           intent!=intent_key.load(std::memory_order_acquire) || serial!=request_serial.load(std::memory_order_acquire)) return;
        if(!identity || root.empty()) {latest_request.reset();pending_request.reset();InvalidateResult();return;}
        const bool changed=!latest_request || latest_request->serial!=serial || !SameRequest(request,*latest_request);
        if(changed) {latest_request=std::move(request);InvalidateResult();}
        if(changed || (!pending_request && !(probing && active_serial==serial) && GetTickCount64()-destination_updated>=400)) {
            pending_request=*latest_request;SetEvent(destination_event);
        }
    }catch(...) {/* Allocation failure must not stop capture or publish a stale result. */}
}
void RefreshSongDestination() noexcept {
    if(!destination_enabled.load(std::memory_order_acquire)) return;
    request_serial.fetch_add(1,std::memory_order_acq_rel);cache_revision.fetch_add(1,std::memory_order_acq_rel);
    refresh_pending.store(true,std::memory_order_release);SetEvent(destination_event);
}
bool ReadSongDestination(std::uint64_t identity,unsigned settings_generation,std::wstring& saved_file) {
    if(!identity || !destination_enabled.load(std::memory_order_acquire) ||
       !TryAcquireSRWLockShared(&destination_lock)) return false;
    struct Unlock {~Unlock(){ReleaseSRWLockShared(&destination_lock);}} unlock;
    if(result_serial!=request_serial.load(std::memory_order_acquire) || result_intent!=intent_key.load(std::memory_order_acquire) || identity!=destination_identity ||
       settings_generation!=destination_generation || destination_file.empty() ||
       GetTickCount64()-destination_updated>3000) return false;
    saved_file=destination_file;
    return true;
}
}
