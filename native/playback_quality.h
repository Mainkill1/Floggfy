#pragma once
#include "flac_history_core.h"
#include "ogg_history_core.h"
#include <array>
#include <deque>
#include <string>
namespace history {
enum class PlaybackFormat { Unknown, Ogg, Flac };
enum class PlaybackLevel { Unknown, Low, Normal, High, VeryHigh, Lossless, Lossless24, Lossy };
PlaybackLevel ParsePlaybackLevel(const std::string &);
struct PlaybackQualitySnapshot {
  std::array<wchar_t, 192> title{};
  PlaybackFormat format = PlaybackFormat::Unknown;
  unsigned rate = 0;
  double bitrate = 0;
  unsigned bits = 0;
  PlaybackLevel level = PlaybackLevel::Unknown;
};
class PlaybackQualityTracker {
  struct Candidate {
    uintptr_t context = 0;
    double born = 0;
    PlaybackFormat format = PlaybackFormat::Unknown;
    Stream ogg;
    FlacInfo flac;
    FlacCoverage coverage;
    std::array<uint8_t, 42> prefix{};
    size_t prefix_size = 0;
    uint64_t bytes = 0;
    bool metadata = false, broken = false, complete = false, coverage_valid = true;
  };
  std::deque<Candidate> streams_;
  std::string identity_;
  std::wstring title_;
  double duration_ = 0, start_ = 0, arrival_ = 0, last_position_ = 0, last_time_ = 0;
  double generation_floor_ = -1, settle_until_ = 0;
  bool playing_ = false;
  Candidate *Find(uintptr_t context, PlaybackFormat format);
  Candidate &Begin(uintptr_t context, PlaybackFormat format, double time);

public:
  void Media(const std::string &key, const std::wstring &title, double position, double duration,
             double time, bool playing = true);
  void Ogg(uintptr_t context, const uint8_t *bytes, size_t length, double time);
  void FlacBegin(uintptr_t context, double time);
  void FlacBytes(uintptr_t context, const uint8_t *prefix, size_t length, size_t encoded_bytes);
  void FlacFrame(uintptr_t context, const uint8_t *frame, size_t length);
  void FlacEnd(uintptr_t context, bool failed);
  void ClearStreams();
  PlaybackQualitySnapshot Snapshot(const std::string &client_quality = {}) const;
};
std::array<std::wstring, 5> PlaybackQualityLabels(const PlaybackQualitySnapshot &);
// Windows bridge: worker publishes a fixed-size snapshot; menu only reads RAM.
void PublishPlaybackQuality(const PlaybackQualitySnapshot &);
PlaybackQualitySnapshot ReadPlaybackQuality();
} // namespace history
