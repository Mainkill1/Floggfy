#pragma once
#include <cstdint>

namespace capture_speed {
constexpr bool Supported(unsigned speed) noexcept {
    return speed==1 || speed==2 || speed==4 || speed==8 || speed==10;
}

// Per-decoder integer phase. The first block is always forwarded and the
// long-run forwarded ratio is exactly 1/speed without floating-point drift.
class Phase {
    unsigned speed_=1,phase_=0;
public:
    bool Forward(unsigned speed) noexcept {
        if(!Supported(speed))speed=1;
        if(speed!=speed_){speed_=speed;phase_=0;}
        const bool forward=phase_==0;
        phase_=(phase_+1)%speed_;
        return forward;
    }
    void Reset() noexcept {speed_=1;phase_=0;}
};
}
