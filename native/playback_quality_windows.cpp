#define WIN32_LEAN_AND_MEAN
#include "playback_quality.h"
#include <type_traits>
#include <windows.h>
namespace history {
namespace {
static_assert(std::is_trivially_copyable<PlaybackQualitySnapshot>::value,
              "fixed-size menu snapshot");
SRWLOCK snapshot_lock = SRWLOCK_INIT;
PlaybackQualitySnapshot current;
ULONGLONG updated = 0;
} // namespace
void PublishPlaybackQuality(const PlaybackQualitySnapshot &value) {
  AcquireSRWLockExclusive(&snapshot_lock);
  current = value;
  updated = GetTickCount64();
  ReleaseSRWLockExclusive(&snapshot_lock);
}
PlaybackQualitySnapshot ReadPlaybackQuality() {
  // Never wait on an audio worker from CEF's menu callback.
  if (!TryAcquireSRWLockShared(&snapshot_lock))
    return {};
  auto value = current;
  auto time = updated;
  ReleaseSRWLockShared(&snapshot_lock);
  if (!time || GetTickCount64() - time > 3000)
    return {};
  return value;
}
} // namespace history
