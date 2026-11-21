#include "../native/media/decoded_output_speed.h"
#include <algorithm>
#include <initializer_list>
#include <cassert>
#include <cstdio>

int main() {
    for(unsigned speed:{1u,2u,4u,8u,10u}) {
        decoded_output::Accelerator accelerator;
        for(size_t frames=1;frames<4096;++frames) {
            const auto kept=accelerator.Keep(reinterpret_cast<void*>(1),frames*2,speed);
            assert(kept==2*std::max(size_t(1),frames/speed));
        }
    }
    std::puts("PASS: stereo frame reduction at every supported speed");
}
