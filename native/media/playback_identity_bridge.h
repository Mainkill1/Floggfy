#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace playback_identity {

constexpr std::size_t kInstanceBytes = 16;
constexpr std::size_t kInstanceHexCharacters = 32;
constexpr std::size_t kPlayerFieldBytes = 32;
constexpr std::size_t kPlayerStateSearchBytes = 0x1000;
constexpr std::size_t kMaximumSnapshotBytes = 0x600;
constexpr std::size_t kObservationCapacity = 128;

struct Instance {
    std::array<unsigned char,kInstanceBytes> bytes{};
    std::array<char,kInstanceHexCharacters> hex{};
};

bool ParseInstance(std::string_view value,Instance& out) noexcept;

enum class Representation : unsigned char { None,RawBytes,AsciiHex };
enum class SnapshotRole : unsigned char { Owner,Decoder,PlayerPlaybackId };

struct PlayerPath {
    std::size_t source_offset=0;
    std::size_t state_offset=0;
    std::size_t playback_id_offset=0;
};

using ReadMemory=bool(*)(const void* source,void* destination,std::size_t size) noexcept;

enum class LocationStatus { WaitingMetadata,WaitingState,Unreadable,NotFound,Ambiguous,Verified };
struct PlayerLocation {
    LocationStatus status=LocationStatus::WaitingMetadata;
    std::size_t offset=0;
    std::size_t matches=0;
    std::size_t readable_bytes=0;
};

// Run only inside a live playback callback. A unique complete metadata ID
// calibrates the field; unrelated state bytes never enter the observation ring.
PlayerLocation LocatePlaybackInstance(const void* owner,const PlayerPath& path,
                                     ReadMemory read,const Instance& expected) noexcept;
std::size_t ReadPlayerState(const void* owner,const PlayerPath& path,ReadMemory read,
                     std::array<unsigned char,kPlayerStateSearchBytes>& bytes) noexcept;

bool ReadPlaybackInstance(const void* owner,const PlayerPath& path,
                          ReadMemory read,Instance& out,
                          std::array<unsigned char,kPlayerFieldBytes>* field=nullptr,
                          std::size_t payload_offset=0) noexcept;

struct Match {
    std::uintptr_t owner=0;
    std::uintptr_t decoder=0;
    unsigned epoch=0;
    std::uint64_t sequence=0;
    std::uint64_t metadata_generation=0;
    std::uint64_t owner_generation=0;
    Representation representation=Representation::None;
    SnapshotRole role=SnapshotRole::Owner;
    std::size_t offset=0;
    double observed_at=0;
    bool ObservedDuring(double began,double closed_at=0) const noexcept {
        return !observed_at||(observed_at>=began&&(!closed_at||observed_at<closed_at));
    }
};

struct ObservationInfo {
    std::uintptr_t owner=0;
    std::uintptr_t decoder=0;
    unsigned epoch=0;
    std::uint64_t sequence=0;
    std::uint64_t owner_generation=0;
    SnapshotRole role=SnapshotRole::Owner;
    double observed_at=0;
};

class DiagnosticBridge {
public:
    bool SetExpected(std::string_view playback_instance,
                     std::uint64_t metadata_generation) noexcept;
    bool NeedsPlayerState(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,double now=0) const noexcept;
    void ObservePlayerState(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                            std::uint64_t sequence,
                            const std::array<unsigned char,kPlayerStateSearchBytes>& bytes,
                            double observed_at,std::size_t readable_bytes=kPlayerStateSearchBytes) noexcept;
    void PlayerStateUnreadable() noexcept {location_.status=LocationStatus::Unreadable;}
    PlayerLocation Location() const noexcept {return location_;}
    bool HasPlayerIdentity(std::uintptr_t decoder,unsigned epoch,double began,double closed_at) const noexcept;
    bool NeedsObservation(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                          SnapshotRole role,const void* snapshot,
                          std::size_t snapshot_size,double minimum_observed_at=0) const noexcept;
    // Returns true when a new observation is retained; matching is queried separately.
    bool Observe(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                 std::uint64_t sequence,const void* snapshot,
                 std::size_t snapshot_size,SnapshotRole role=SnapshotRole::Owner,
                 double observed_at=0,double minimum_observed_at=0) noexcept;
    std::size_t QueryMatches(std::string_view playback_instance,
                             std::uint64_t metadata_generation,unsigned epoch,
                             Match* matches,std::size_t capacity) const noexcept;
    void Reset() noexcept;
    std::optional<ObservationInfo> TakeObservation() noexcept;
    bool ObservationLost() const noexcept {return observation_lost_;}
    std::uint64_t CalibrationRetirements() const noexcept {return calibration_retirements_;}

private:
    struct Observation {
        std::uintptr_t owner=0;
        std::uintptr_t decoder=0;
        unsigned epoch=0;
        std::uint64_t sequence=0;
        std::uint64_t owner_generation=0;
        SnapshotRole role=SnapshotRole::Owner;
        double observed_at=0;
        std::size_t size=0;
        std::array<unsigned char,kMaximumSnapshotBytes> bytes{};
    };

    bool MatchObservation(const Observation& observation,const Instance& expected,
                          std::uint64_t metadata_generation,Match& match) const noexcept;
    void LocateExpected() noexcept;

    struct PlayerStateObservation {
        std::uintptr_t owner=0,decoder=0;
        unsigned epoch=0;
        std::uint64_t sequence=0,metadata_generation=0;
        double observed_at=0;
        std::size_t readable_bytes=0;
        bool retry=false;
        bool location_cached=false;
        PlayerLocation location{};
        std::array<unsigned char,kPlayerStateSearchBytes> bytes{};
    };
    // Startup only: preserve native evidence even if its decoder ends before
    // metadata arrives. Release these snapshots after learning the field.
    std::array<PlayerStateObservation,16> player_states_{};
    std::size_t player_state_count_=0,player_state_next_=0;
    std::uint64_t calibration_retirements_=0;
    PlayerLocation location_{};

    std::array<Observation,kObservationCapacity> observations_{};
    std::size_t observation_head_=0;
    std::size_t observation_count_=0;
    std::uint64_t last_owner_generation_=0;
    Instance expected_{};
    std::uint64_t metadata_generation_=0;
    bool has_expected_=false;
    bool observation_lost_=false;
    std::optional<ObservationInfo> pending_observation_;
};

} // namespace playback_identity
