#include "../native/media/playback_identity_bridge.h"

#include <array>
#include <cassert>
#include <cstring>

using namespace playback_identity;

namespace {

constexpr char kFirst[] = "00112233445566778899aabbccddeeff";
constexpr char kSecond[] = "ffeeddccbbaa99887766554433221100";
constexpr char kThird[] = "1234567890abcdef1122334455667788";

std::array<unsigned char, kMaximumSnapshotBytes> Snapshot(const Instance& instance,
                                                           size_t offset,
                                                           bool ascii = false) {
    std::array<unsigned char, kMaximumSnapshotBytes> bytes{};
    if (ascii) std::memcpy(bytes.data() + offset, instance.hex.data(), instance.hex.size());
    else std::memcpy(bytes.data() + offset, instance.bytes.data(), instance.bytes.size());
    return bytes;
}

std::optional<Match> QuerySingle(const DiagnosticBridge& bridge,const char* id,
                                 std::uint64_t generation,unsigned epoch) {
    std::array<Match,kObservationCapacity> matches{};
    const auto count=bridge.QueryMatches(id,generation,epoch,matches.data(),matches.size());
    assert(count<=1);
    return count?std::optional<Match>(matches[0]):std::nullopt;
}

bool CopyMemory(const void* source,void* destination,std::size_t size) noexcept {
    std::memcpy(destination,source,size);
    return true;
}

bool RefuseMemory(const void*,void*,std::size_t) noexcept {
    return false;
}

std::uintptr_t bounded_state=0;
std::size_t readable_state=0;
bool CopyBoundedState(const void* source,void* destination,std::size_t size) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    if(address>=bounded_state&&address<bounded_state+kPlayerStateSearchBytes&&
       (address-bounded_state>=readable_state||size>readable_state-(address-bounded_state)))return false;
    return CopyMemory(source,destination,size);
}

}

int main() {
    Instance first{};
    assert(ParseInstance(kFirst, first));
    assert(first.bytes[0] == 0x00 && first.bytes[1] == 0x11 && first.bytes[15] == 0xff);
    assert(std::memcmp(first.hex.data(), kFirst, first.hex.size()) == 0);

    Instance upper{};
    assert(ParseInstance("00112233445566778899AABBCCDDEEFF", upper));
    assert(upper.bytes == first.bytes);
    assert(std::memcmp(upper.hex.data(), kFirst, upper.hex.size()) == 0);

    Instance invalid{};
    assert(!ParseInstance("0011", invalid));
    assert(!ParseInstance("00112233445566778899aabbccddeefg", invalid));
    assert(!ParseInstance("00000000000000000000000000000000", invalid));

    std::array<unsigned char,0x100> owner{};
    std::array<unsigned char,0x240> source{};
    std::array<unsigned char,0x1000> state{};
    const auto source_pointer=reinterpret_cast<std::uintptr_t>(source.data());
    const auto state_pointer=reinterpret_cast<std::uintptr_t>(state.data());
    std::memcpy(owner.data()+0x78,&source_pointer,sizeof(source_pointer));
    std::memcpy(source.data()+0x1e8,&state_pointer,sizeof(state_pointer));
    std::memcpy(state.data()+0x9b8,first.bytes.data(),first.bytes.size());
    Instance through_player{};
    const PlayerPath current_client_path{0x78,0x1e8,0x9b8};
    assert(ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player));
    assert(through_player.bytes==first.bytes&&through_player.hex==first.hex);
    assert(!ReadPlaybackInstance(owner.data(),current_client_path,RefuseMemory,through_player));
    std::memset(state.data()+0x9b8,0,first.bytes.size());
    assert(!ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player));

    // Captured field layout: an eight-byte native prefix before the 16-byte ID.
    // Confirm both this layout and the older inline payload against metadata.
    for(const size_t payload_offset:{size_t{0},size_t{8}}) {
        std::fill(state.begin()+0x9b8,state.end(),0);
        if(payload_offset) {
            const unsigned char prefix[]={0x50,0x58,0xad,0x18,0x07,0x02,0,0};
            std::copy(std::begin(prefix),std::end(prefix),state.begin()+0x9b8);
        }
        std::memcpy(state.data()+0x9b8+payload_offset,first.bytes.data(),first.bytes.size());
        std::array<unsigned char,kPlayerFieldBytes> field{};
        assert(ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field));
        DiagnosticBridge layout;assert(layout.SetExpected(kFirst,1));
        assert(layout.Observe(0x1000,0x2000,0,1,field.data(),field.size(),SnapshotRole::PlayerPlaybackId));
        const auto match=QuerySingle(layout,kFirst,1,0);assert(match&&match->offset==payload_offset);
        assert(ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field,match->offset));
        assert(through_player.bytes==first.bytes);
        assert(!ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field,kPlayerFieldBytes));
    }

    // Discover from the exact local metadata ID, without a fixed field address.
    // 0xa08 is the observed 1.3.10.2586 layout; other positions ensure discovery
    // does not merely select from a list of known Spotify versions.
    for(const size_t location:{size_t{0x9b8},size_t{0x9c0},size_t{0xa08},size_t{0x5f7}}) {
        state.fill(0);
        std::memcpy(state.data()+location,first.bytes.data(),first.bytes.size());
        const auto located=LocatePlaybackInstance(owner.data(),current_client_path,CopyMemory,first);
        assert(located.status==LocationStatus::Verified&&located.offset==location&&located.matches==1);
        PlayerPath learned=current_client_path;learned.playback_id_offset=located.offset;
        std::array<unsigned char,kPlayerFieldBytes> field{};
        assert(ReadPlaybackInstance(owner.data(),learned,CopyMemory,through_player,&field));
        DiagnosticBridge layout;
        // Native evidence can arrive before the corresponding metadata.
        assert(layout.Observe(0x1000,0x2000,0,1,field.data(),field.size(),SnapshotRole::PlayerPlaybackId));
        assert(layout.SetExpected(kFirst,1));
        const auto learned_match=QuerySingle(layout,kFirst,1,0);assert(learned_match&&learned_match->offset==0);
        // A preload with a different ID at the learned location stays readable.
        Instance next{};assert(ParseInstance(kSecond,next));
        std::memcpy(state.data()+location,next.bytes.data(),next.bytes.size());
        assert(ReadPlaybackInstance(owner.data(),learned,CopyMemory,through_player,&field));
        assert(through_player.bytes==next.bytes);
    }
    assert(LocatePlaybackInstance(owner.data(),current_client_path,RefuseMemory,first).status==LocationStatus::Unreadable);
    state.fill(0);
    assert(LocatePlaybackInstance(owner.data(),current_client_path,CopyMemory,first).status==LocationStatus::NotFound);
    std::memcpy(state.data()+0x100,first.bytes.data(),first.bytes.size());
    std::memcpy(state.data()+0xa08,first.bytes.data(),first.bytes.size());
    const auto duplicate_location=LocatePlaybackInstance(owner.data(),current_client_path,CopyMemory,first);
    assert(duplicate_location.status==LocationStatus::Ambiguous&&duplicate_location.matches==2);

    state.fill(0);std::memcpy(state.data()+0xa08,first.bytes.data(),first.bytes.size());
    std::memcpy(state.data()+0xb00,first.bytes.data(),first.bytes.size());
    bounded_state=reinterpret_cast<std::uintptr_t>(state.data());readable_state=0xa40;
    const auto bounded_location=LocatePlaybackInstance(owner.data(),current_client_path,CopyBoundedState,first);
    assert(bounded_location.status==LocationStatus::Verified&&bounded_location.offset==0xa08&&bounded_location.readable_bytes==readable_state);
    readable_state=0xa10;
    assert(LocatePlaybackInstance(owner.data(),current_client_path,CopyBoundedState,first).status!=LocationStatus::Verified);

    {
        DiagnosticBridge discovery;
        state.fill(0);std::memcpy(state.data()+0xa08,first.bytes.data(),first.bytes.size());
        assert(discovery.NeedsPlayerState(0x1000,0x2000,3));
        discovery.ObservePlayerState(0x1000,0x2000,3,1,state,10.5);
        assert(!discovery.NeedsPlayerState(0x1000,0x2000,3));
        assert(discovery.Location().status==LocationStatus::WaitingMetadata);
        // Decoder owner is gone before metadata arrives; only retained evidence
        // may be used, and its timestamp must not be changed to metadata time.
        state.fill(0);
        assert(discovery.SetExpected(kFirst,1));
        assert(discovery.Location().status==LocationStatus::Verified&&discovery.Location().offset==0xa08);
        const auto late=QuerySingle(discovery,kFirst,1,3);
        assert(late&&late->observed_at==10.5&&late->ObservedDuring(10,11)&&!late->ObservedDuring(11,12));
        assert(discovery.HasPlayerIdentity(0x2000,3,10,11));
        assert(!discovery.HasPlayerIdentity(0x2000,3,11,12));
        assert(!discovery.HasPlayerIdentity(0x2000,4,10,11));
        assert(!discovery.NeedsPlayerState(0x3000,0x4000,3));
        // The learned field can retain a next-track ID before its metadata.
        Instance next{};assert(ParseInstance(kSecond,next));
        std::array<unsigned char,kPlayerFieldBytes> preload{};
        std::copy(next.bytes.begin(),next.bytes.end(),preload.begin());
        assert(discovery.Observe(0x3000,0x4000,3,2,preload.data(),preload.size(),SnapshotRole::PlayerPlaybackId,11.5));
        assert(discovery.Location().offset==0xa08);
        assert(discovery.SetExpected(kSecond,2));
        const auto next_match=QuerySingle(discovery,kSecond,2,3);assert(next_match&&next_match->decoder==0x4000&&next_match->observed_at==11.5);
        discovery.Reset();assert(discovery.Location().status==LocationStatus::WaitingMetadata);
        state.fill(0);std::memcpy(state.data()+0x100,first.bytes.data(),first.bytes.size());
        std::memcpy(state.data()+0xa08,first.bytes.data(),first.bytes.size());
        discovery.ObservePlayerState(0x1000,0x2000,3,3,state,12);
        assert(discovery.SetExpected(kFirst,3));
        assert(discovery.Location().status==LocationStatus::Ambiguous&&!QuerySingle(discovery,kFirst,3,3));
    }
    {
        DiagnosticBridge delayed_initialization;assert(delayed_initialization.SetExpected(kFirst,1));
        state.fill(0);
        delayed_initialization.ObservePlayerState(1,2,0,1,state,10);
        assert(delayed_initialization.Location().status==LocationStatus::NotFound);
        assert(!delayed_initialization.NeedsPlayerState(1,2,0,10.01));
        for(unsigned i=1;i<=100;++i) {
            const double time=10+i*.3;
            assert(delayed_initialization.NeedsPlayerState(1,2,0,time));
            delayed_initialization.ObservePlayerState(1,2,0,i+1,state,time);
        }
        assert(!delayed_initialization.ObservationLost());
        std::memcpy(state.data()+0xa08,first.bytes.data(),first.bytes.size());
        delayed_initialization.ObservePlayerState(1,2,0,102,state,41);
        assert(delayed_initialization.Location().status==LocationStatus::Verified);
        const auto initialized=QuerySingle(delayed_initialization,kFirst,1,0);assert(initialized&&initialized->observed_at==41);
        assert(!delayed_initialization.HasPlayerIdentity(2,0,10,40));
    }

    DiagnosticBridge bridge;
    assert(bridge.SetExpected(kFirst, 7));
    auto raw = Snapshot(first, 0x128);
    assert(bridge.NeedsObservation(0x1000, 0x2000, 3, SnapshotRole::Owner, raw.data(), raw.size()));
    assert(bridge.Observe(0x1000, 0x2000, 3, 10, raw.data(), raw.size()));
    auto observed = bridge.TakeObservation();
    assert(observed && observed->owner == 0x1000 && observed->decoder == 0x2000);
    assert(observed->epoch == 3 && observed->sequence == 10 && observed->owner_generation == 1);
    assert(!bridge.TakeObservation());
    assert(!bridge.NeedsObservation(0x1000, 0x2000, 3, SnapshotRole::Owner, raw.data(), raw.size()));
    assert(bridge.NeedsObservation(0x1000, 0x3000, 3, SnapshotRole::Owner, raw.data(), raw.size()));
    assert(bridge.NeedsObservation(0x1000, 0x2000, 4, SnapshotRole::Owner, raw.data(), raw.size()));
    auto match = QuerySingle(bridge,kFirst,7,3);
    assert(match && match->owner == 0x1000 && match->decoder == 0x2000);
    assert(match->epoch == 3 && match->sequence == 10 && match->metadata_generation == 7);
    assert(match->owner_generation == 1 && match->representation == Representation::RawBytes);
    assert(match->offset == 0x128);
    assert(QuerySingle(bridge,kFirst,7,3)->sequence==10); // Queries retain evidence.

    DiagnosticBridge decoder_snapshot;
    assert(decoder_snapshot.SetExpected(kFirst, 12));
    std::array<unsigned char,kMaximumSnapshotBytes> owner_empty{};
    assert(decoder_snapshot.Observe(0xa000,0xb000,0,1,owner_empty.data(),owner_empty.size(),
                                     SnapshotRole::Owner));
    assert(decoder_snapshot.NeedsObservation(0xa000,0xb000,0,SnapshotRole::Decoder,raw.data(),raw.size()));
    assert(decoder_snapshot.Observe(0xa000,0xb000,0,2,raw.data(),raw.size(),
                                    SnapshotRole::Decoder));
    match=QuerySingle(decoder_snapshot,kFirst,12,0);
    assert(match&&match->role==SnapshotRole::Decoder&&match->offset==0x128);

    // Spotify can keep the playback-pump/decoder pair alive while a DJ stage
    // hands off to music. Soggfy read PlaybackId on every callback; retain the
    // changed value while deduplicating an unchanged one.
    DiagnosticBridge changing_identity;
    assert(changing_identity.Observe(0xa100,0xb100,0,1,raw.data(),raw.size(),
                                      SnapshotRole::PlayerPlaybackId));
    assert(!changing_identity.Observe(0xa100,0xb100,0,2,raw.data(),raw.size(),
                                      SnapshotRole::PlayerPlaybackId));
    Instance handoff{};assert(ParseInstance(kSecond,handoff));
    auto changed=Snapshot(handoff,0x128);
    assert(changing_identity.Observe(0xa100,0xb100,0,3,changed.data(),changed.size(),
                                      SnapshotRole::PlayerPlaybackId));
    assert(changing_identity.SetExpected(kSecond,14));
    match=QuerySingle(changing_identity,kSecond,14,0);
    assert(match&&match->sequence==3&&match->owner==0xa100&&match->decoder==0xb100);
    assert(QuerySingle(changing_identity,kFirst,14,0)->sequence==1); // Duplicate snapshot was not retained.

    // One Spotify playback can drive an interstitial decoder and its music
    // decoder. Preserve both exact owner matches so media-kind policy can
    // exclude the wrong codec without discarding the valid stream.
    DiagnosticBridge shared_playback;
    assert(shared_playback.Observe(0xc000,0xd000,0,1,raw.data(),raw.size(),
                                    SnapshotRole::PlayerPlaybackId));
    assert(shared_playback.Observe(0xe000,0xf000,0,2,raw.data(),raw.size(),
                                    SnapshotRole::PlayerPlaybackId));
    assert(shared_playback.SetExpected(kFirst,13));
    std::array<Match,kObservationCapacity> shared{};
    assert(shared_playback.QueryMatches(kFirst,13,0,shared.data(),shared.size())==2);
    assert(shared[0].owner==0xc000&&shared[1].owner==0xe000);
    assert(shared[0].metadata_generation==13&&shared[1].metadata_generation==13);

    // Metadata can arrive after the first decoder callback; retained bounded
    // evidence must still resolve the exact instance.
    DiagnosticBridge metadata_late;
    assert(metadata_late.Observe(0x6000, 0x7000, 5, 50, raw.data(), raw.size()));
    assert(metadata_late.SetExpected(kFirst, 10));
    match = QuerySingle(metadata_late,kFirst,10,5);
    assert(match && match->owner == 0x6000 && match->decoder == 0x7000);
    assert(match->metadata_generation == 10 && match->sequence == 50);

    // Correctness queries are non-destructive. The worker may see the exact
    // playback ID before decoded output makes its capture bindable, so the
    // same retained observation must still be available on the next pass.
    DiagnosticBridge retryable;
    assert(retryable.Observe(0x6100,0x7100,6,51,raw.data(),raw.size(),
                              SnapshotRole::PlayerPlaybackId));
    std::array<Match,kObservationCapacity> queried{};
    assert(retryable.QueryMatches(kFirst,20,6,queried.data(),queried.size())==1);
    assert(queried[0].decoder==0x7100&&queried[0].metadata_generation==20&&
           queried[0].epoch==6);
    queried={};
    assert(retryable.QueryMatches(kFirst,20,6,queried.data(),queried.size())==1&&
           queried[0].sequence==51&&
           "a failed first binding attempt cannot consume exact identity evidence");
    assert(retryable.QueryMatches(kFirst,20,7,queried.data(),queried.size())==0&&
           "an observation from an earlier capture epoch cannot bind a new session");
    retryable.Reset();
    assert(retryable.QueryMatches(kFirst,20,6,queried.data(),queried.size())==0&&
           "capture settings reset all retained direct identity evidence");

    // An address can be reused while late metadata still needs an older
    // snapshot. Time belongs to the observation, not to the metadata query.
    DiagnosticBridge lifecycles;
    assert(lifecycles.Observe(0x6200,0x7200,6,60,raw.data(),raw.size(),
                              SnapshotRole::PlayerPlaybackId,90.0));
    assert(lifecycles.QueryMatches(kFirst,21,6,queried.data(),queried.size())==1);
    assert(queried[0].observed_at==90.0&&queried[0].ObservedDuring(80.0,100.0));
    assert(!queried[0].ObservedDuring(100.0)&&
           "historical A evidence cannot claim B after the decoder address is reused");
    assert(!queried[0].ObservedDuring(80.0,90.0)&&
           "the next capture closes the predecessor's identity interval exclusively");
    assert(!lifecycles.NeedsObservation(0x6200,0x7200,6,SnapshotRole::PlayerPlaybackId,
                                       raw.data(),raw.size(),80.0));
    assert(lifecycles.NeedsObservation(0x6200,0x7200,6,SnapshotRole::PlayerPlaybackId,
                                      raw.data(),raw.size(),100.0));
    assert(lifecycles.Observe(0x6200,0x7200,6,61,raw.data(),raw.size(),
                              SnapshotRole::PlayerPlaybackId,100.1,100.0));
    assert(!lifecycles.NeedsObservation(0x6200,0x7200,6,SnapshotRole::PlayerPlaybackId,
                                       raw.data(),raw.size(),100.0));
    assert(!lifecycles.Observe(0x6200,0x7200,6,62,raw.data(),raw.size(),
                              SnapshotRole::PlayerPlaybackId,100.2,100.0));
    assert(lifecycles.QueryMatches(kFirst,22,6,queried.data(),queried.size())==2&&
           queried[0].sequence==60&&queried[0].observed_at==90.0&&
           queried[1].sequence==61&&queried[1].observed_at==100.1&&
           queried[1].ObservedDuring(100.0)&&
           "refreshing unchanged pre-BOS fields preserves the old capture's historical evidence");
    Match legacy{};assert(legacy.ObservedDuring(100.0,101.0));

    // Capture epoch zero is the normal initial settings epoch.
    DiagnosticBridge initial_epoch;
    assert(initial_epoch.SetExpected(kFirst, 1));
    assert(initial_epoch.NeedsObservation(0x8000, 0x9000, 0, SnapshotRole::Owner, raw.data(), raw.size()));
    assert(initial_epoch.Observe(0x8000, 0x9000, 0, 1, raw.data(), raw.size()));
    match = QuerySingle(initial_epoch,kFirst,1,0);
    assert(match && match->epoch == 0);

    // The same owner with a replacement decoder is a new owner generation.
    Instance second{};
    assert(ParseInstance(kSecond, second));
    assert(bridge.SetExpected(kSecond, 8));
    auto ascii = Snapshot(second, 0x220, true);
    assert(bridge.Observe(0x1000, 0x3000, 3, 20, ascii.data(), ascii.size()));
    match = QuerySingle(bridge,kSecond,8,3);
    assert(match && match->decoder == 0x3000 && match->owner_generation == 2);
    assert(match->representation == Representation::AsciiHex && match->offset == 0x220);

    // Reusing both pointers in another capture epoch cannot inherit the old generation.
    Instance third{};
    assert(ParseInstance(kThird, third));
    auto reused = Snapshot(third, 0x128);
    assert(bridge.SetExpected(kThird, 9));
    assert(bridge.Observe(0x1000, 0x2000, 4, 30, reused.data(), reused.size()));
    match = QuerySingle(bridge,kThird,9,4);
    assert(match && match->epoch == 4 && match->owner_generation == 3);

    // Two occurrences are ambiguous and must not produce an identity observation.
    auto duplicate = raw;
    std::memcpy(duplicate.data() + 0x300, first.bytes.data(), first.bytes.size());
    DiagnosticBridge ambiguous;
    assert(ambiguous.SetExpected(kFirst, 11));
    assert(ambiguous.Observe(0x4000, 0x5000, 4, 40, duplicate.data(), duplicate.size()));
    assert(!QuerySingle(ambiguous,kFirst,11,4));

    // An unsupported player layout can outlive the startup calibration ring.
    // Retiring unverified raw guesses is not loss of established ID evidence.
    {
        DiagnosticBridge uncalibrated;
        std::array<unsigned char,kPlayerStateSearchBytes> unknown{};
        for(size_t i=0;i<40;++i) {
            assert(uncalibrated.SetExpected(kFirst,i+1));
            uncalibrated.ObservePlayerState(0x1000+i,0x2000+i,0,i+1,unknown,100+i);
            assert(uncalibrated.Location().status==LocationStatus::NotFound);
            assert(!uncalibrated.ObservationLost());
        }
        assert(uncalibrated.CalibrationRetirements()==24);
        std::memcpy(unknown.data()+0xa08,first.bytes.data(),first.bytes.size());
        uncalibrated.ObservePlayerState(0x9000,0xa000,0,41,unknown,141);
        assert(uncalibrated.Location().status==LocationStatus::Verified);
        auto recovered=QuerySingle(uncalibrated,kFirst,40,0);
        assert(recovered&&recovered->decoder==0xa000&&recovered->observed_at==141);
    }

    // Fixed storage reports loss instead of silently overwriting evidence.
    DiagnosticBridge bounded;
    for (size_t index = 0; index < kObservationCapacity; ++index) {
        std::array<unsigned char, kMaximumSnapshotBytes> empty{};
        assert(bounded.Observe(0x10000 + index, 0x20000 + index, 1, index + 1,
                                empty.data(), empty.size()));
    }
    std::array<unsigned char, kMaximumSnapshotBytes> empty{};
    assert(bounded.Observe(0x9999, 0xaaaa, 1, 99, empty.data(), empty.size()));
    assert(bounded.ObservationLost());

    // A long-running Spotify process must not permanently disable direct
    // identity after the bounded history fills. A new lifecycle must still enter the
    // bridge and become matchable after more than 128 prior observations.
    DiagnosticBridge long_session;
    for(size_t index=0;index<kObservationCapacity;++index) {
        std::array<unsigned char,kMaximumSnapshotBytes> prior{};
        prior[0]=static_cast<unsigned char>(index+1);
        assert(long_session.NeedsObservation(0x30000+index,0x40000+index,1,
            SnapshotRole::PlayerPlaybackId,prior.data(),prior.size()));
        assert(long_session.Observe(0x30000+index,0x40000+index,1,index+1,
            prior.data(),prior.size(),SnapshotRole::PlayerPlaybackId));
    }
    assert(long_session.NeedsObservation(0x50000,0x60000,1,
        SnapshotRole::PlayerPlaybackId,changed.data(),changed.size()));
    assert(long_session.Observe(0x50000,0x60000,1,kObservationCapacity+1,
        changed.data(),changed.size(),SnapshotRole::PlayerPlaybackId));
    assert(long_session.ObservationLost());
    assert(long_session.SetExpected(kSecond,15));
    match=QuerySingle(long_session,kSecond,15,1);
    assert(match&&match->owner==0x50000&&match->decoder==0x60000&&
           match->sequence==kObservationCapacity+1);
}
