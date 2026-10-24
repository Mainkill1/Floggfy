#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
#include <memory>
namespace history {
struct Media {
    std::wstring title, artist, album, album_artist, app, genre;
    unsigned track = 0;
    double position = 0, duration = 0;
    bool playing = false;
    int playback_status = -1; // Raw Windows status; unknown for other providers.
    std::string Key() const;
};
std::string Utf8(const std::wstring& s);
std::string Json(const std::string& s);
enum class MediaRead { Pending,Ready,Failed };
class MediaReader {
    void* manager_ = nullptr;
    bool initialized_ = false;
public:
    ~MediaReader();
    // Blocking WinRT boundary: dedicated observer worker or external probe only.
    // Capture callers must use MediaSnapshotReader instead.
    bool Read(Media& out);
    bool Read(Media& out,double& observed_at);
    // Pending means session/state/bounds or discontinuous playback changed
    // while properties were outstanding. Normal timeline refreshes are allowed.
    MediaRead Observe(Media& out,double& observed_at);
};
class MediaSnapshotReader {
public:
    // Optional fetch boundary is invoked only on the observer thread. Its
    // context must outlive outstanding work, including after nonblocking Stop.
    using Fetch=bool(*)(Media&,double& observed_at,void* context);
    explicit MediaSnapshotReader(Fetch fetch=nullptr,void* context=nullptr) noexcept;
    ~MediaSnapshotReader();
    MediaSnapshotReader(const MediaSnapshotReader&)=delete;
    MediaSnapshotReader& operator=(const MediaSnapshotReader&)=delete;
    // Start/Stop/TryRead belong to one consumer thread. Start does no COM work;
    // it cannot replace a stopped observer until its outstanding work finishes.
    bool Start() noexcept;
    void Stop() noexcept;
    bool IsStopped() const noexcept;
    // RAM-only try-lock. Keep sequence across calls and capture epoch changes.
    // Ready/Failed consumes a new sequence; Pending never replays observations.
    // Failed leaves Media untouched. Ready older than 3s is rejected. The
    // consumer must also invalidate a >3s absence while Pending is returned.
    MediaRead TryRead(Media&,double& observed_at,uint64_t& sequence) noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    Fetch fetch_;
    void* context_;
    static DWORD WINAPI Run(void*);
};
}
