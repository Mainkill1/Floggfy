// Instrument the real locator without adding counters or test APIs to production.
#include "../native/playback_identity_bridge.cpp"
#include <cassert>

static unsigned location_scans=0;
extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* function,void*) {
    if(function==reinterpret_cast<void*>(&playback_identity::LocateInState))++location_scans;
}
extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void*,void*) {}

int main() {
    using namespace playback_identity;
    constexpr auto first="00112233445566778899aabbccddeeff";
    constexpr auto second="ffeeddccbbaa99887766554433221100";
    DiagnosticBridge bridge;
    std::array<unsigned char,kPlayerStateSearchBytes> state{};
    assert(bridge.SetExpected(first,1));
    bridge.ObservePlayerState(1,2,0,1,state,10);
    bridge.ObservePlayerState(1,2,0,2,state,10.3);
    const auto scans=location_scans;
    for(unsigned i=2;i<=102;++i) {
        const double now=10+i*.3;
        assert(!bridge.NeedsPlayerState(1,2,0,now-.1));
        assert(bridge.NeedsPlayerState(1,2,0,now));
        bridge.ObservePlayerState(1,2,0,i+1,state,now);
        assert(bridge.Location().status==LocationStatus::NotFound);
    }
    assert(location_scans==scans&&"unchanged retained calibration snapshots must not be rescanned");
    assert(bridge.SetExpected(first,2));
    assert(location_scans==scans&&"metadata generation alone does not change a location search");
    assert(bridge.SetExpected(second,3));
    assert(location_scans==scans+2&&"changing the expected ID invalidates both retained snapshots");
    Instance expected{};assert(ParseInstance(second,expected));
    std::memcpy(state.data()+0x100,expected.bytes.data(),expected.bytes.size());
    bridge.ObservePlayerState(1,2,0,104,state,42,0);
    assert(bridge.Location().status==LocationStatus::Unreadable);
    bridge.ObservePlayerState(1,2,0,105,state,42.3,0x110);
    assert(bridge.Location().status==LocationStatus::Unreadable);
    // Bytes are unchanged, but a complete readable field must invalidate the result.
    bridge.ObservePlayerState(1,2,0,106,state,42.6,0x120);
    assert(bridge.Location().status==LocationStatus::Verified&&bridge.Location().offset==0x100);
    Match matches[4]{};
    assert(bridge.QueryMatches(second,4,0,matches,4)==1);
    assert(matches[0].sequence==106&&matches[0].observed_at==42.6);

    bridge.Reset();
    state.fill(0);
    std::memcpy(state.data()+0x100,expected.bytes.data(),expected.bytes.size());
    std::memcpy(state.data()+0x200,expected.bytes.data(),expected.bytes.size());
    bridge.ObservePlayerState(3,4,7,1,state,50);
    assert(bridge.SetExpected(second,1));
    assert(bridge.Location().status==LocationStatus::Ambiguous);
    const auto ambiguous_scans=location_scans;
    assert(bridge.SetExpected(second,2));
    assert(location_scans==ambiguous_scans&&bridge.Location().status==LocationStatus::Ambiguous);
    assert(bridge.SetExpected(first,3));
    assert(bridge.Location().status==LocationStatus::NotFound);
    assert(bridge.SetExpected(second,4));
    assert(bridge.Location().status==LocationStatus::Ambiguous);
    assert(bridge.QueryMatches(second,4,7,matches,4)==0);
}
