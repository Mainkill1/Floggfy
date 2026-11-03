#pragma once
#include <cstdint>

namespace capture_speed {
constexpr bool Supported(unsigned speed) noexcept {
    return speed==1 || speed==2 || speed==4 || speed==8 || speed==10;
}

}
