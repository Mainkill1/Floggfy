#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace playback_identity {

constexpr std::size_t kInstanceBytes = 16;
constexpr std::size_t kInstanceHexCharacters = 32;
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

bool ReadPlaybackInstance(const void* owner,const PlayerPath& path,
                          ReadMemory read,Instance& out) noexcept;

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
};

struct ObservationInfo {
    std::uintptr_t owner=0;
    std::uintptr_t decoder=0;
    unsigned epoch=0;
    std::uint64_t sequence=0;
    std::uint64_t owner_generation=0;
    SnapshotRole role=SnapshotRole::Owner;
};

class DiagnosticBridge {
public:
    bool SetExpected(std::string_view playback_instance,
                     std::uint64_t metadata_generation) noexcept;
    bool NeedsObservation(std::uintptr_t owner,std::uintptr_t decoder,
                          unsigned epoch,SnapshotRole role=SnapshotRole::Owner) const noexcept;
    bool NeedsObservation(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                          SnapshotRole role,const void* snapshot,
                          std::size_t snapshot_size) const noexcept;
    bool Observe(std::uintptr_t owner,std::uintptr_t decoder,unsigned epoch,
                 std::uint64_t sequence,const void* snapshot,
                 std::size_t snapshot_size,SnapshotRole role=SnapshotRole::Owner) noexcept;
    std::optional<Match> TakeMatch() noexcept;
    std::optional<ObservationInfo> TakeObservation() noexcept;
    bool ObservationLost() const noexcept {return observation_lost_;}

private:
    struct Observation {
        std::uintptr_t owner=0;
        std::uintptr_t decoder=0;
        unsigned epoch=0;
        std::uint64_t sequence=0;
        std::uint64_t owner_generation=0;
        std::uint64_t reported_metadata_generation=0;
        SnapshotRole role=SnapshotRole::Owner;
        std::size_t size=0;
        std::array<unsigned char,kMaximumSnapshotBytes> bytes{};
    };

    bool MatchObservation(const Observation& observation,Match& match) const noexcept;
    bool ResolveExpected() noexcept;

    std::array<Observation,kObservationCapacity> observations_{};
    std::size_t observation_count_=0;
    std::uint64_t next_owner_generation_=1;
    Instance expected_{};
    std::uint64_t metadata_generation_=0;
    bool has_expected_=false;
    bool observation_lost_=false;
    std::array<Match,kObservationCapacity> pending_matches_{};
    std::size_t pending_match_count_=0;
    std::size_t pending_match_next_=0;
    std::optional<ObservationInfo> pending_observation_;
};

} // namespace playback_identity
