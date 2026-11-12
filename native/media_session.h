#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
#include <memory>
#include "media_diagnostics.h"
namespace history {
enum class MediaSource { Windows,Spotify };
struct Media {
    std::wstring title, artist, album, album_artist, app, genre;
    unsigned track = 0;
    double position = 0, duration = 0;
    unsigned playback_speed = 1;
    bool playing = false;
    int playback_status = -1; // Raw Windows status; unknown for other providers.
    MediaSource source=MediaSource::Windows;
    std::string spotify_uri,playback_instance;
    bool explicit_skip=false; // Validated local skip action attached to a transition.
    bool explicit_control=false; // Any observed local playback control, including seeks.
    bool prior_control=false; // A control on the prior instance acknowledged by this Ready successor.
    bool control_monitoring=false; // Action listener was attached for this transition.
    bool natural_transition=false; // Positive renderer transition evidence; absent is unknown.
    bool terminal_context=false; // Explicit monitored episode context clearance, anchored to the old instance.
    bool transition_pending=false; // Natural-transition candidate awaiting a successor playback ID.
    std::string Key() const;
    std::string ListenKey() const {
        if(source==MediaSource::Spotify)
            return spotify_uri+(playback_instance.empty()?std::string{}:
                "\x1f"+playback_instance);
        return spotify_uri.empty()?Key():spotify_uri;
    }
};
std::string Utf8(const std::wstring& s);
enum class MediaRead { Pending,Ready,Failed,Transition };
enum class ClientMediaRead { Pending,Ready,Unavailable,Blocked,Transition };
// Validated renderer snapshots only. Publishing/polling never performs I/O.
bool PublishClientMedia(Media,double sampled_utc_ms);
void PublishClientBufferingTransition(Media,double sampled_utc_ms,double changed_utc_ms);
void InvalidateClientMedia() noexcept;
void PublishClientMediaUnavailable(const char* static_reason,bool blocked=false);
ClientMediaRead TryReadClientMedia(Media&,double& observed_at,uint64_t& sequence,MediaDiagnostic&) noexcept;
class MediaReader {
    void* manager_ = nullptr;
    bool initialized_ = false;
    MediaDiagnostic diagnostic_;
public:
    ~MediaReader();
    // Blocking WinRT boundary: dedicated observer worker or external probe only.
    // Capture callers must use MediaSnapshotReader instead.
    bool Read(Media& out);
    bool Read(Media& out,double& observed_at);
    // Pending means session/state/bounds or discontinuous playback changed
    // while properties were outstanding. Normal timeline refreshes are allowed.
    MediaRead Observe(Media& out,double& observed_at);
    const MediaDiagnostic& Diagnostic() const noexcept {return diagnostic_;}
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
    MediaRead TryRead(Media&,double& observed_at,uint64_t& sequence,MediaDiagnostic* diagnostic=nullptr) noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    Fetch fetch_;
    void* context_;
    static DWORD WINAPI Run(void*);
};
// App snapshots are primary. Explicit remote/buffering states block capture;
// missing/incompatible app snapshots permit WinRT only after explicit opt-in.
class PlaybackSnapshotReader {
    MediaSnapshotReader windows_;
    uint64_t client_sequence_=0,windows_sequence_=0,sequence_=0;
    bool running_=false,windows_started_=false,windows_fallback_=false;
public:
    explicit PlaybackSnapshotReader(MediaSnapshotReader::Fetch fetch=nullptr,void* context=nullptr) noexcept:windows_(fetch,context){}
    bool Start() noexcept {running_=true;return true;}
    void Stop() noexcept {running_=false;windows_started_=false;windows_.Stop();}
    void SetWindowsFallback(bool enabled) noexcept;
    bool IsStopped() const noexcept {return windows_.IsStopped();}
    MediaRead TryRead(Media&,double& observed_at,uint64_t& sequence,MediaDiagnostic* diagnostic=nullptr) noexcept;
};
}
