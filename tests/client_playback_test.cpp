#include "../native/client_playback.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    using namespace history;
    if(argc==2&&std::string(argv[1])=="--stdin") {
        const std::string payload(std::istreambuf_iterator<char>(std::cin),{});
        ClientPlaybackRecord record;std::string failure;
        if(!ParseClientPlayback(payload,record,failure)) {
            std::cerr<<"collector payload rejected: "<<failure<<'\n';return 1;
        }
        assert(record.metadata.uri=="spotify:track:0000000000000000000001");
        assert(record.instance=="playback-1"&&record.position==12.5&&!record.explicit_control&&!record.prior_control);
        std::cout<<"PASS: real collector Ready payload satisfies native playback protocol\n";
        return 0;
    }
    assert(argc==1);
    const std::string base="v=1&title=Song&artist=Artist&album=Album&uri=spotify:track:0000000000000000000001&duration=180";
    const std::string timeline="&position=12.5&sampled_ms=1793300000000&paused=0&buffering=0&local=1&speed=4";
    ClientPlaybackRecord value;std::string error;
    assert(ParseClientPlayback(base+timeline,value,error));
    assert(value.metadata.uri=="spotify:track:0000000000000000000001"&&value.position==12.5&&value.sampled_ms==1793300000000&&value.playing&&!value.buffering&&value.local==1&&value.speed==4);
    assert(!ParseClientPlayback(base,value,error));
    assert(!ParseClientPlayback(base+timeline+"&position=13",value,error));
    assert(!ParseClientPlayback(base+timeline+"&UNKNOWN=ignored",value,error));
    assert(ParseClientPlayback(base+timeline+"&controlled=1",value,error)&&value.explicit_control);
    assert(!ParseClientPlayback(base+timeline+"&controlled=unknown",value,error));
    assert(ParseClientPlayback(base+timeline+"&prior_control=1",value,error)&&value.prior_control);
    assert(!ParseClientPlayback(base+timeline+"&prior_control=unknown",value,error));
    for(const auto* bad:{"0","3","5","11","-1","fast"})
        assert(!ParseClientPlayback(base+"&position=0&sampled_ms=1793300000000&paused=0&buffering=0&local=1&speed="+bad,value,error));
    for(const auto* bad:{"nan","inf","-1","181","","12junk"})
        assert(!ParseClientPlayback(base+"&position="+bad+"&sampled_ms=1793300000000&paused=0&buffering=0&local=1",value,error));
    assert(!ParseClientPlayback("v=1&title=Song&artist=Artist&album=Album&uri=spotify:track:short&duration=180"+timeline,value,error));
    assert(ParseClientPlayback(base+"&position=0&sampled_ms=1793300000000&paused=1&buffering=0&local=0",value,error));
    assert(!value.playing&&value.local==0);
    assert(ParseClientPlayback(base+"&position=0&sampled_ms=1793300000000&paused=0&buffering=1&local=unknown",value,error));
    assert(value.buffering&&value.local==-1);
    ClientTransition transition;
    assert(!ParseClientPlayback(base+"&position="+std::string("0\0junk",6)+"&sampled_ms=1793300000000&paused=0&buffering=0&local=1",value,error));
    const std::string event="v=1&blocked=buffering&uri=spotify%3Atrack%3A0000000000000000000001&instance=playback-1&sampled_ms=1793300000500&transition_ms=1793300000000&position_ms=0&manual=1&controlled=1&monitoring=1";
    assert(ParseClientTransition(event,transition));
    assert(!transition.natural);
    assert(ParseClientTransition(event+"&natural=1",transition)&&transition.natural);
    assert(ParseClientTransition(event+"&pending=1",transition)&&transition.pending);
    assert(!ParseClientTransition(event+"&pending=1&natural=1",transition));
    assert(!ParseClientTransition(event+"&pending=unknown",transition));
    assert(!ParseClientTransition(event+"&natural=unknown",transition));
    assert(!ParseClientTransition(event+std::string("\0junk",5),transition));
    assert(!ParseClientTransition(std::string(16385,'x'),transition));
    assert(transition.uri=="spotify:track:0000000000000000000001"&&transition.instance=="playback-1"&&transition.position==0&&transition.transition_ms==1793300000000&&transition.explicit_skip&&transition.explicit_control&&transition.control_monitoring);
    assert(!ParseClientTransition(event+"&controlled=unknown",transition));
    assert(!ParseClientTransition(event+"&monitoring=unknown",transition));
    assert(!ParseClientTransition(event+"&transition_ms=1793300000000",transition));
    assert(!ParseClientTransition(event+"&unexpected=1",transition));
    assert(!ParseClientTransition("v=1&blocked=buffering",transition));
    std::cout<<"PASS: bounded client timeline protocol, exact URI, finite positions, flags and duplicate/unknown-field rejection\n";
}
