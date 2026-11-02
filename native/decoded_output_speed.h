#pragma once
#include <atomic>
#include <cstddef>
#include "capture_speed.h"
namespace decoded_output {
struct Span {void* data=nullptr;std::size_t size=0;};
// The validated caller consumes stereo float frames. Per-call rounding keeps
// complete frames and requires no decoder-address or lifetime state. The rate
// is approximate by at most one frame per invocation.
class Accelerator {
    std::atomic<bool> enabled_{true};
public:
    std::size_t Keep(void* context,std::size_t produced,unsigned speed,unsigned) noexcept {
        if(!Enabled())return produced;
        if(!context||!capture_speed::Supported(speed)||(produced%2)) {
            Disable();return produced;
        }
        if(speed==1||!produced)return produced;
        const auto frames=produced/2;
        const auto keep=frames/speed;
        return (keep?keep:1)*2;
    }
    bool Enabled() const noexcept {return enabled_.load(std::memory_order_acquire);}
    void Disable() noexcept {enabled_.store(false,std::memory_order_release);}
    void Reset() noexcept {enabled_.store(true,std::memory_order_release);}
};
}
