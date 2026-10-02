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
ULONGLONG updated = 0, client_updated = 0;
PlaybackQualitySnapshot client;
} // namespace
void PublishPlaybackQuality(const PlaybackQualitySnapshot &value) {
  AcquireSRWLockExclusive(&snapshot_lock);
  current = value;
  updated = GetTickCount64();
  ReleaseSRWLockExclusive(&snapshot_lock);
}
void PublishClientPlaybackQuality(const PlaybackQualitySnapshot &value) {
  AcquireSRWLockExclusive(&snapshot_lock);
  client = value;
  client_updated = GetTickCount64();
  ReleaseSRWLockExclusive(&snapshot_lock);
}
PlaybackQualitySnapshot ReadPlaybackQuality() {
  // Never wait on an audio worker from CEF's menu callback.
  if (!TryAcquireSRWLockShared(&snapshot_lock))
    return {};
  auto value = current;
  auto time = updated;
  auto actual = client;
  auto actual_time = client_updated;
  ReleaseSRWLockShared(&snapshot_lock);
  const auto now = GetTickCount64();
  if (actual_time) {
    // Once a client snapshot exists, never fall back to a lagging SMTC title.
    if (now - actual_time > 3000) return {};
    if (!actual.identity || actual.identity != value.identity || !time || now - time > 3000)
      return actual;
    value.title = actual.title;
    if (actual.level != PlaybackLevel::Unknown) {
      const bool lossless = actual.level == PlaybackLevel::Lossless || actual.level == PlaybackLevel::Lossless24;
      if ((value.format == PlaybackFormat::Flac && !lossless) ||
          (value.format == PlaybackFormat::Ogg && lossless) ||
          (value.format == PlaybackFormat::Flac && actual.level == PlaybackLevel::Lossless24 && value.bits != 24)) return actual;
      value.level = actual.level;
    }
    return value;
  }
  if (!time || now - time > 3000) return {};
  return value;
}
} // namespace history
