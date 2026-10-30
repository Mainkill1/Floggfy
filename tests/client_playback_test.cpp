#include "../native/client_playback.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace history;
    const std::string base="v=1&title=Song&artist=Artist&album=Album&uri=spotify:track:0000000000000000000001&duration=180";
    const std::string timeline="&position=12.5&sampled_ms=1793300000000&paused=0&buffering=0&local=1";
    ClientPlaybackRecord value;std::string error;
    assert(ParseClientPlayback(base+timeline,value,error));
    assert(value.metadata.uri=="spotify:track:0000000000000000000001"&&value.position==12.5&&value.sampled_ms==1793300000000&&value.playing&&!value.buffering&&value.local==1);
    assert(!ParseClientPlayback(base,value,error));
    assert(!ParseClientPlayback(base+timeline+"&position=13",value,error));
    assert(!ParseClientPlayback(base+timeline+"&UNKNOWN=ignored",value,error));
    for(const auto* bad:{"nan","inf","-1","181","","12junk"})
        assert(!ParseClientPlayback(base+"&position="+bad+"&sampled_ms=1793300000000&paused=0&buffering=0&local=1",value,error));
    assert(!ParseClientPlayback("v=1&title=Song&artist=Artist&album=Album&uri=spotify:track:short&duration=180"+timeline,value,error));
    assert(ParseClientPlayback(base+"&position=0&sampled_ms=1793300000000&paused=1&buffering=0&local=0",value,error));
    assert(!value.playing&&value.local==0);
    assert(ParseClientPlayback(base+"&position=0&sampled_ms=1793300000000&paused=0&buffering=1&local=unknown",value,error));
    assert(value.buffering&&value.local==-1);
    ClientTransition transition;
    const std::string event="v=1&blocked=buffering&uri=spotify%3Atrack%3A0000000000000000000001&instance=playback-1&sampled_ms=1793300000500&transition_ms=1793300000000&position_ms=0";
    assert(ParseClientTransition(event,transition));
    assert(transition.uri=="spotify:track:0000000000000000000001"&&transition.instance=="playback-1"&&transition.position==0&&transition.transition_ms==1793300000000);
    assert(!ParseClientTransition(event+"&transition_ms=1793300000000",transition));
    assert(!ParseClientTransition(event+"&unexpected=1",transition));
    assert(!ParseClientTransition("v=1&blocked=buffering",transition));
    std::cout<<"PASS: bounded client timeline protocol, exact URI, finite positions, flags and duplicate/unknown-field rejection\n";
}
