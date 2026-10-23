#pragma once
#include "media_session.h"
#include <array>
#include <atomic>
#include <memory>
namespace history {
// Strict SMTC identity, including duration and source application. No allocation.
bool SameArtworkIdentity(const Media& first,const Media& second) noexcept;
class WindowsThumbnailCache {
public:
    using Fetch=bool(*)(const Media& expected,Media& result,void* context);
    using Current=bool(*)(const Media& expected,void* context);
    using Clock=ULONGLONG(*)(void* context) noexcept;
    explicit WindowsThumbnailCache(Fetch fetch,void* context=nullptr,Current current=nullptr,Clock clock=nullptr) noexcept;
    ~WindowsThumbnailCache();
    WindowsThumbnailCache(const WindowsThumbnailCache&)=delete;
    WindowsThumbnailCache& operator=(const WindowsThumbnailCache&)=delete;
    // Queue/coalesce only. Never initialize COM, fetch, wait for a cache lock or
    // copy artwork on the caller. One active fetch plus one pending identity.
    void Request(const Media& media) noexcept;
    // RAM-only try-lock; retained tracks remain readable after a track change.
    // Failure leaves both outputs unchanged, including allocation failures.
    bool Get(const Media& media,std::vector<uint8_t>& bytes,std::wstring& extension) noexcept;
    // Off the capture/UI paths; callers must quiesce Request/Get before stopping.
    void Stop() noexcept;
private:
    struct Entry {
        std::shared_ptr<const Media> identity;
        std::vector<uint8_t> bytes;
        std::wstring extension;
        uint64_t area=0,order=0;
        ULONGLONG updated=0;
    };
    static constexpr size_t retained_limit=8,byte_limit=4*1024*1024;
    Fetch fetch_;
    void* context_;
    Current current_check_;
    Clock clock_;
    SRWLOCK start_lock_=SRWLOCK_INIT,state_lock_=SRWLOCK_INIT;
    std::atomic<HANDLE> thread_{nullptr};
    HANDLE event_=nullptr;
    std::atomic<bool> running_{false};
    std::shared_ptr<const Media> current_,pending_;
    std::array<std::shared_ptr<const Entry>,retained_limit> entries_{};
    uint64_t serial_=0,pending_serial_=0,active_serial_=0,order_=0;
    ULONGLONG retry_at_=0;
    ULONGLONG Now() const noexcept;
    bool Start() noexcept;
    static DWORD WINAPI Run(void* context);
    void Work() noexcept;
};
}
