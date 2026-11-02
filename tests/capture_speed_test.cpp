#include "../native/capture_speed.h"
#include "../native/decoded_output_speed.h"
#include "../native/ogg_history_core.h"
#include <cassert>
#include <cstdio>

int main() {
    for(unsigned rate:{1u,2u,4u,8u,10u}) {
        history::Listen delayed;delayed.Observe("old",0,20,true,100,rate);
        for(double t=.1;t<20.0/rate;t+=.1)delayed.Observe("old",t*rate,20,true,100+t,rate);
        const double end=100+20.0/rate;
        delayed.Observe("old",.1,20,true,end+.1/rate,rate);
        delayed.Observe("new",.1+.6*rate,20,true,end+.1/rate+.6,rate);
        assert(delayed.eligible&&"delayed identity preserves accelerated observed start");
        history::Listen polled;polled.Observe("old",0,20,true,100,rate);
        for(unsigned sample=1;sample<20;++sample)polled.Observe("old",sample,20,true,100+double(sample)/rate,rate);
        assert(polled.Observe("new",.3*rate,20,true,end+.3,rate)=="old");
        assert(polled.eligible&&"300ms polling delay preserves a start proven by the previous natural end");
        history::Listen unknown;unknown.Observe("new",3,20,true,100,rate);
        assert(!unknown.eligible&&"mid-track initial observation without boundary evidence remains rejected");
    }
    for(unsigned speed:{1u,2u,4u,8u,10u}) {
        decoded_output::Accelerator accelerator;
        for(size_t frames=1;frames<4096;++frames) {
            const auto kept=accelerator.Keep(reinterpret_cast<void*>(1),frames*2,speed);
            assert(kept==2*std::max(size_t(1),frames/speed));
        }
    }

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
    std::puts("PASS: shipped stereo reduction and rate-bound delayed-identity listen generations");
}
