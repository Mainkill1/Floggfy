#pragma once
#include <cstdint>
#include <vector>
namespace artwork_fixture {
// Synthetic 2x2 RGBA, encoded with libwebp lossless. Expected pixels in test.
inline std::vector<uint8_t> webp_alpha() { return {82,73,70,70,48,0,0,0,87,69,66,80,86,80,56,76,35,0,0,0,47,1,64,0,16,31,48,255,2,130,34,255,71,19,16,20,249,63,154,64,128,144,198,127,148,0,182,174,196,5,26,34,250,31,1,0}; }
inline std::vector<uint8_t> webp_lossy() { return {0x52,0x49,0x46,0x46,0x22,0,0,0,0x57,0x45,0x42,0x50,0x56,0x50,0x38,0x20,0x16,0,0,0,0x30,1,0,0x9d,1,0x2a,1,0,1,0,0x0e,0xc0,0xfe,0x25,0xa4,0,3,0x70,0,0,0,0}; }
}
