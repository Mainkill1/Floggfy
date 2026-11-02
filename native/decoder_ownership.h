#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ownership {

bool NormalizeDecoderOwner(std::uintptr_t capture_context,
                           std::int32_t capture_to_decoder,
                           std::uintptr_t& owner) noexcept;

enum class DecoderCodec : std::uint8_t { Ogg, Flac, Aac };
enum class OwnershipState : std::uint8_t {
    AwaitingOutput,
    AwaitingPlaybackInstance,
    Bound,
    Conflict,
    Invalidated,
};
enum class CaptureAbortReason : std::uint8_t {
    DecoderError,
    InvalidData,
    Reinitialized,
    MemoryLimit,
    QueueLoss,
    SettingsChange,
    Timeout,
    Shutdown,
};
enum class ObservationLoss : std::uint8_t {
    DecodedQueue,
    CaptureQueue,
    Media,
    Epoch,
};

struct DecodedActivity {
    std::uintptr_t owner=0;
    std::uint64_t calls=0;
    std::uint64_t produced_floats=0;
    std::uint64_t consumed_bytes=0;
    std::uint64_t kept_floats=0;
    double first=0;
    double last=0;
    unsigned epoch=0;
};

struct OwnershipBinding {
    std::uint64_t capture_token=0;
    std::uint64_t playback_token=0;
    std::uint64_t listen_generation=0;
    OwnershipState state=OwnershipState::AwaitingOutput;
};

class DecoderOwnershipTracker {
public:
    std::uint64_t BeginCapture(DecoderCodec codec,std::uintptr_t owner,
                               std::uint32_t stream,double time,unsigned epoch);
    void CompleteCapture(std::uint64_t capture_token,double time);
    void AbortCapture(std::uint64_t capture_token,CaptureAbortReason reason);
    void ObserveDecoded(const DecodedActivity& activity);
    void ObservePlayback(const std::string& playback_id,const std::string& uri,
                         std::uint64_t listen_generation,double observed);
    void ObserveTransition(const std::string& playback_id,const std::string& uri,
                           double transition_time,bool explicit_skip,
                           bool natural_transition);
    void ObservationLost(ObservationLoss source,double time);
    void Reset(unsigned epoch);
    OwnershipBinding Binding(std::uint64_t capture_token) const;

private:
    static constexpr std::size_t kCaptureCapacity=32;
    static constexpr std::size_t kOwnerCapacity=16;
    static constexpr std::size_t kPlaybackCapacity=16;

    struct CaptureRecord {
        std::uint64_t token=0;
        std::uint64_t sequence=0;
        std::uintptr_t owner=0;
        std::uint32_t stream=0;
        DecoderCodec codec=DecoderCodec::Ogg;
        OwnershipState state=OwnershipState::AwaitingOutput;
        double begun=0;
        double first_output=0;
        double last_output=0;
        double completed_at=0;
        unsigned epoch=0;
        std::uint64_t playback_token=0;
        std::uint64_t listen_generation=0;
        bool active=false;
        bool complete=false;
    };
    struct OwnerRecord {
        std::uintptr_t owner=0;
        std::uint64_t token=0;
        std::uint64_t sequence=0;
        double first=0;
        double last=0;
        unsigned epoch=0;
    };
    struct PlaybackRecord {
        std::string id;
        std::string uri;
        std::uint64_t token=0;
        std::uint64_t sequence=0;
        std::uint64_t listen_generation=0;
        double observed=0;
    };

    CaptureRecord* FindCapture(std::uint64_t token) noexcept;
    const CaptureRecord* FindCapture(std::uint64_t token) const noexcept;
    OwnerRecord* FindOwner(std::uintptr_t owner) noexcept;
    PlaybackRecord* FindPlayback(const std::string& id) noexcept;
    PlaybackRecord* FindPlayback(std::uint64_t token) noexcept;
    OwnerRecord& EnsureOwner(std::uintptr_t owner,unsigned epoch);
    PlaybackRecord& EnsurePlayback(const std::string& id,const std::string& uri,
                                   std::uint64_t generation,double observed);
    void Expire(double now) noexcept;
    void InvalidateOwner(std::uintptr_t owner) noexcept;
    void InvalidatePlayback(std::uint64_t playback) noexcept;
    void Conflict(CaptureRecord& capture) noexcept;
    void TryBind(CaptureRecord& capture,PlaybackRecord& playback,double observed) noexcept;
    void ObservePlaybackImpl(const std::string& playback_id,const std::string& uri,
                             std::uint64_t listen_generation,double observed);

    std::array<CaptureRecord,kCaptureCapacity> captures_{};
    std::array<OwnerRecord,kOwnerCapacity> owners_{};
    std::array<PlaybackRecord,kPlaybackCapacity> playbacks_{};
    std::uint64_t next_capture_=1;
    std::uint64_t next_owner_=1;
    std::uint64_t next_playback_=1;
    std::uint64_t sequence_=1;
    std::uint64_t current_capture_=0;
    std::uint64_t pending_capture_=0;
    std::uint64_t current_playback_=0;
    std::uint64_t latest_playback_=0;
    double latest_decoded_time_=0;
    unsigned epoch_=0;
};

} // namespace ownership
