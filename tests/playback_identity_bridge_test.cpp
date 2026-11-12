#include "../native/playback_identity_bridge.h"

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

bool CopyMemory(const void* source,void* destination,std::size_t size) noexcept {
    std::memcpy(destination,source,size);
    return true;
}

bool RefuseMemory(const void*,void*,std::size_t) noexcept {
    return false;
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
    std::array<unsigned char,0xa00> state{};
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
        std::fill(state.begin()+0x9b8,state.begin()+0x9b8+kPlayerFieldBytes,0);
        if(payload_offset) {
            const unsigned char prefix[]={0x50,0x58,0xad,0x18,0x07,0x02,0,0};
            std::copy(std::begin(prefix),std::end(prefix),state.begin()+0x9b8);
        }
        std::memcpy(state.data()+0x9b8+payload_offset,first.bytes.data(),first.bytes.size());
        std::array<unsigned char,kPlayerFieldBytes> field{};
        assert(ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field));
        DiagnosticBridge layout;assert(layout.SetExpected(kFirst,1));
        assert(layout.Observe(0x1000,0x2000,0,1,field.data(),field.size(),SnapshotRole::PlayerPlaybackId));
        const auto match=layout.TakeMatch();assert(match&&match->offset==payload_offset);
        assert(ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field,match->offset));
        assert(through_player.bytes==first.bytes);
        assert(!ReadPlaybackInstance(owner.data(),current_client_path,CopyMemory,through_player,&field,kPlayerFieldBytes));
    }

    DiagnosticBridge bridge;
    assert(bridge.SetExpected(kFirst, 7));
    assert(bridge.NeedsObservation(0x1000, 0x2000, 3));
    auto raw = Snapshot(first, 0x128);
    assert(bridge.Observe(0x1000, 0x2000, 3, 10, raw.data(), raw.size()));
    auto observed = bridge.TakeObservation();
    assert(observed && observed->owner == 0x1000 && observed->decoder == 0x2000);
    assert(observed->epoch == 3 && observed->sequence == 10 && observed->owner_generation == 1);
    assert(!bridge.TakeObservation());
    assert(!bridge.NeedsObservation(0x1000, 0x2000, 3));
    assert(bridge.NeedsObservation(0x1000, 0x3000, 3));
    assert(bridge.NeedsObservation(0x1000, 0x2000, 4));
    auto match = bridge.TakeMatch();
    assert(match && match->owner == 0x1000 && match->decoder == 0x2000);
    assert(match->epoch == 3 && match->sequence == 10 && match->metadata_generation == 7);
    assert(match->owner_generation == 1 && match->representation == Representation::RawBytes);
    assert(match->offset == 0x128);
    assert(!bridge.TakeMatch());

    DiagnosticBridge decoder_snapshot;
    assert(decoder_snapshot.SetExpected(kFirst, 12));
    std::array<unsigned char,kMaximumSnapshotBytes> owner_empty{};
    assert(!decoder_snapshot.Observe(0xa000,0xb000,0,1,owner_empty.data(),owner_empty.size(),
                                     SnapshotRole::Owner));
    assert(decoder_snapshot.NeedsObservation(0xa000,0xb000,0,SnapshotRole::Decoder));
    assert(decoder_snapshot.Observe(0xa000,0xb000,0,2,raw.data(),raw.size(),
                                    SnapshotRole::Decoder));
    match=decoder_snapshot.TakeMatch();
    assert(match&&match->role==SnapshotRole::Decoder&&match->offset==0x128);

    // Spotify can keep the playback-pump/decoder pair alive while a DJ stage
    // hands off to music. Soggfy read PlaybackId on every callback; retain the
    // changed value while deduplicating an unchanged one.
    DiagnosticBridge changing_identity;
    assert(!changing_identity.Observe(0xa100,0xb100,0,1,raw.data(),raw.size(),
                                      SnapshotRole::PlayerPlaybackId));
    assert(!changing_identity.Observe(0xa100,0xb100,0,2,raw.data(),raw.size(),
                                      SnapshotRole::PlayerPlaybackId));
    Instance handoff{};assert(ParseInstance(kSecond,handoff));
    auto changed=Snapshot(handoff,0x128);
    assert(!changing_identity.Observe(0xa100,0xb100,0,3,changed.data(),changed.size(),
                                      SnapshotRole::PlayerPlaybackId));
    assert(changing_identity.SetExpected(kSecond,14));
    match=changing_identity.TakeMatch();
    assert(match&&match->sequence==3&&match->owner==0xa100&&match->decoder==0xb100);
    assert(!changing_identity.TakeMatch());

    // One Spotify playback can drive an interstitial decoder and its music
    // decoder. Preserve both exact owner matches so media-kind policy can
    // exclude the wrong codec without discarding the valid stream.
    DiagnosticBridge shared_playback;
    assert(!shared_playback.Observe(0xc000,0xd000,0,1,raw.data(),raw.size(),
                                    SnapshotRole::PlayerPlaybackId));
    assert(!shared_playback.Observe(0xe000,0xf000,0,2,raw.data(),raw.size(),
                                    SnapshotRole::PlayerPlaybackId));
    assert(shared_playback.SetExpected(kFirst,13));
    const auto shared_first=shared_playback.TakeMatch();
    const auto shared_second=shared_playback.TakeMatch();
    assert(shared_first&&shared_second&&!shared_playback.TakeMatch());
    assert(shared_first->owner==0xc000&&shared_second->owner==0xe000);
    assert(shared_first->metadata_generation==13&&shared_second->metadata_generation==13);

    // Metadata can arrive after the first decoder callback; retained bounded
    // evidence must still resolve the exact instance.
    DiagnosticBridge metadata_late;
    assert(!metadata_late.Observe(0x6000, 0x7000, 5, 50, raw.data(), raw.size()));
    assert(metadata_late.SetExpected(kFirst, 10));
    match = metadata_late.TakeMatch();
    assert(match && match->owner == 0x6000 && match->decoder == 0x7000);
    assert(match->metadata_generation == 10 && match->sequence == 50);

    // Correctness queries are non-destructive. The worker may see the exact
    // playback ID before decoded output makes its capture bindable, so the
    // same retained observation must still be available on the next pass.
    DiagnosticBridge retryable;
    assert(!retryable.Observe(0x6100,0x7100,6,51,raw.data(),raw.size(),
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
    assert(!lifecycles.Observe(0x6200,0x7200,6,60,raw.data(),raw.size(),
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
    assert(lifecycles.NeedsObservation(0x6200,0x7200,6,SnapshotRole::PlayerPlaybackId,100.0));
    assert(!lifecycles.Observe(0x6200,0x7200,6,61,raw.data(),raw.size(),
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
    assert(initial_epoch.NeedsObservation(0x8000, 0x9000, 0));
    assert(initial_epoch.Observe(0x8000, 0x9000, 0, 1, raw.data(), raw.size()));
    match = initial_epoch.TakeMatch();
    assert(match && match->epoch == 0);

    // The same owner with a replacement decoder is a new owner generation.
    Instance second{};
    assert(ParseInstance(kSecond, second));
    assert(bridge.SetExpected(kSecond, 8));
    auto ascii = Snapshot(second, 0x220, true);
    assert(bridge.Observe(0x1000, 0x3000, 3, 20, ascii.data(), ascii.size()));
    match = bridge.TakeMatch();
    assert(match && match->decoder == 0x3000 && match->owner_generation == 2);
    assert(match->representation == Representation::AsciiHex && match->offset == 0x220);

    // Reusing both pointers in another capture epoch cannot inherit the old generation.
    Instance third{};
    assert(ParseInstance(kThird, third));
    auto reused = Snapshot(third, 0x128);
    assert(bridge.SetExpected(kThird, 9));
    assert(bridge.Observe(0x1000, 0x2000, 4, 30, reused.data(), reused.size()));
    match = bridge.TakeMatch();
    assert(match && match->epoch == 4 && match->owner_generation == 3);

    // Two occurrences are ambiguous and must not produce an identity observation.
    auto duplicate = raw;
    std::memcpy(duplicate.data() + 0x300, first.bytes.data(), first.bytes.size());
    DiagnosticBridge ambiguous;
    assert(ambiguous.SetExpected(kFirst, 11));
    assert(!ambiguous.Observe(0x4000, 0x5000, 4, 40, duplicate.data(), duplicate.size()));
    assert(!ambiguous.TakeMatch());

    // Fixed storage reports loss instead of silently overwriting evidence.
    DiagnosticBridge bounded;
    for (size_t index = 0; index < kObservationCapacity; ++index) {
        std::array<unsigned char, kMaximumSnapshotBytes> empty{};
        assert(!bounded.Observe(0x10000 + index, 0x20000 + index, 1, index + 1,
                                empty.data(), empty.size()));
    }
    std::array<unsigned char, kMaximumSnapshotBytes> empty{};
    assert(!bounded.Observe(0x9999, 0xaaaa, 1, 99, empty.data(), empty.size()));
    assert(bounded.ObservationLost());

    // A long-running Spotify process must not permanently disable direct
    // identity after the bounded history fills. The hook calls
    // NeedsObservation before Observe, so a new lifecycle must still enter the
    // bridge and become matchable after more than 128 prior observations.
    DiagnosticBridge long_session;
    for(size_t index=0;index<kObservationCapacity;++index) {
        std::array<unsigned char,kMaximumSnapshotBytes> prior{};
        prior[0]=static_cast<unsigned char>(index+1);
        assert(long_session.NeedsObservation(0x30000+index,0x40000+index,1,
            SnapshotRole::PlayerPlaybackId,prior.data(),prior.size()));
        assert(!long_session.Observe(0x30000+index,0x40000+index,1,index+1,
            prior.data(),prior.size(),SnapshotRole::PlayerPlaybackId));
    }
    assert(long_session.NeedsObservation(0x50000,0x60000,1,
        SnapshotRole::PlayerPlaybackId,changed.data(),changed.size()));
    assert(!long_session.Observe(0x50000,0x60000,1,kObservationCapacity+1,
        changed.data(),changed.size(),SnapshotRole::PlayerPlaybackId));
    assert(long_session.ObservationLost());
    assert(long_session.SetExpected(kSecond,15));
    match=long_session.TakeMatch();
    assert(match&&match->owner==0x50000&&match->decoder==0x60000&&
           match->sequence==kObservationCapacity+1);
}
