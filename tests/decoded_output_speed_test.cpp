#include "../native/decoded_output_speed.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

int main() {
    decoded_output::Accelerator accelerator;
    auto* first=reinterpret_cast<void*>(0x1000);
    assert(accelerator.Keep(first,100,4,1)==24);
    assert(accelerator.Keep(first,2,4,1)==2);
    assert(accelerator.Keep(first,6,4,1)==2);
    assert(accelerator.Keep(first,100,2,2)==50); // Epoch changes cannot retain stale state.
    assert(accelerator.Keep(first,100,1,2)==100);

    // Stateless accounting supports arbitrary decoder lifetimes and pointer reuse.
    for(std::uintptr_t i=1;i<=256;++i)
        assert(accelerator.Keep(reinterpret_cast<void*>(0x1000+i*0x10),80,10,3)==8);
    assert(accelerator.Enabled());
    assert(accelerator.Keep(nullptr,80,10,3)==80&&!accelerator.Enabled());
    assert(accelerator.Keep(first,80,10,3)==80);
    accelerator.Reset();assert(accelerator.Keep(first,3,4,1)==3&&!accelerator.Enabled());
    std::puts("PASS: whole stereo frames, stateless decoder reuse, bounded rounding and invalid-state fallback");
}
