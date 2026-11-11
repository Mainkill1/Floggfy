#pragma once
#include <cstddef>
namespace history {
constexpr unsigned kDefaultBufferedMiB=512;
constexpr unsigned kMaximumBufferedMiB=1024;
constexpr std::size_t kMaximumRecordingBytes=std::size_t{kMaximumBufferedMiB}*1024*1024;
}
