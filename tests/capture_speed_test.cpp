#include "../native/capture_speed.h"
#include "../native/ogg_history_core.h"
#include <cassert>
#include <cstdio>

int main() {
    for(unsigned speed:{1u,2u,4u,8u,10u}) {
        capture_speed::Phase phase;
        unsigned forwarded=0;
        for(unsigned i=0;i<speed*4;++i) {
            const bool forward=phase.Forward(speed);
            assert(forward==(i%speed==0));
            forwarded+=forward;
        }
        assert(forwarded==4);
    }
    capture_speed::Phase first,second;
    assert(first.Forward(4)&&!first.Forward(4));
    assert(second.Forward(4)&&!second.Forward(4));
    first.Reset();assert(first.Forward(10));

    for(unsigned speed:{1u,2u,4u,8u,10u}) {
        history::Listen completed;
        constexpr double duration=20,step_time=.25;
        assert(completed.Observe("song",0,duration,true,100,speed).empty());
        double time=100,position=0;
        while(position+step_time*speed<duration) {
            time+=step_time;position+=step_time*speed;
            assert(completed.Observe("song",position,duration,true,time,speed).empty());
            assert(completed.eligible);
        }
        time+=step_time;
        assert(completed.Observe("next",0,duration,true,time,speed)=="song");
        assert(completed.completed_time==time);
    }

    history::Listen listen;
    assert(listen.Observe("track",0,100,true,10,4).empty());
    const auto generation=listen.generation;
    assert(listen.eligible&&listen.capture_speed==4);
    assert(listen.Observe("track",1,100,true,10.25,4).empty());
    assert(listen.eligible&&listen.generation==generation);
    assert(listen.Observe("track",2,100,true,10.5,8).empty());
    assert(!listen.eligible&&listen.generation==generation);
    // A later speed change cannot revive the invalidated generation. A fresh
    // near-zero identity observation starts a new generation explicitly.
    assert(listen.Observe("next",0,100,true,11,8).empty());
    assert(listen.eligible&&listen.capture_speed==8&&listen.generation==generation+1);
    std::puts("PASS: exact capture phases, independent contexts and rate-bound listen generations");
}
