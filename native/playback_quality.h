#pragma once
#include "flac_history_core.h"
#include "ogg_history_core.h"
#include "library_layout.h"
#include <array>
#include <deque>
#include <string>
#include <string_view>
namespace history {
enum class PlaybackFormat { Unknown, Ogg, Flac, Mp3, Aac };
enum class PlaybackLevel { Unknown, Low, Normal, High, VeryHigh, Lossless, Lossless24, Lossy };
PlaybackLevel ParsePlaybackLevel(const std::string &);
struct PlaybackQualitySnapshot {
  std::array<wchar_t, 192> title{};
  uint64_t identity = 0; // Song identity for menu lookups.
  PlaybackFormat format = PlaybackFormat::Unknown;
  unsigned rate = 0;
  unsigned channels = 0;
  unsigned bits = 0;
  PlaybackLevel level = PlaybackLevel::Unknown;
  bool decoder_bound = false;
  bool display_only = false; // Provisional matcher result; never authorize Auto Skip.

  // Only decoder ownership evidence may identify an exact play for Auto Skip.
  void SetPlaybackInstance(std::string_view instance) noexcept {
    playback_instance_.fill(0);
    if (!instance.empty() && instance.size() < playback_instance_.size() && instance.find('\0') == std::string_view::npos)
      instance.copy(playback_instance_.data(), instance.size());
  }
  bool MatchesPlaybackInstance(std::string_view instance) const noexcept {
    return !instance.empty() && playback_instance_[0] && instance.size() < playback_instance_.size() &&
           playback_instance_[instance.size()] == 0 &&
           std::string_view(playback_instance_.data(), instance.size()) == instance;
  }
private:
  std::array<char, 129> playback_instance_{};
};
PlaybackQualitySnapshot WithPlaybackEncoding(PlaybackQualitySnapshot,const Quality&);
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
    bool metadata = false, broken = false, complete = false, coverage_valid = true;
  };
  std::deque<Candidate> streams_;
  std::string identity_, playback_instance_;
  std::wstring title_;
  double duration_ = 0, start_ = 0, arrival_ = 0, last_position_ = 0, last_time_ = 0;
  double generation_floor_ = -1, settle_until_ = 0;
  bool playing_ = false;
  Candidate *Find(uintptr_t context, PlaybackFormat format);
  Candidate &Begin(uintptr_t context, PlaybackFormat format, double time);

public:
  void Media(const std::string &key, const std::wstring &title, double position, double duration,
             double time, bool playing = true, const std::string &playback_instance = {});
  void Ogg(uintptr_t context, const uint8_t *bytes, size_t length, double time);
  void FlacBegin(uintptr_t context, double time);
  void FlacBytes(uintptr_t context, const uint8_t *prefix, size_t length, size_t encoded_bytes);
  void FlacFrame(uintptr_t context, const uint8_t *frame, size_t length);
  void FlacEnd(uintptr_t context, bool failed);
  void ClearStreams();
  PlaybackQualitySnapshot Snapshot(const std::string &client_quality = {}) const;
};
uint64_t PlaybackIdentity(const std::string &);
std::array<std::wstring, 4> PlaybackQualityLabels(const PlaybackQualitySnapshot &);
// Windows bridge: worker publishes a fixed-size snapshot; menu only reads RAM.
void PublishPlaybackQuality(const PlaybackQualitySnapshot &);
void PublishClientPlaybackQuality(const PlaybackQualitySnapshot &);
PlaybackQualitySnapshot ReadPlaybackQuality();
} // namespace history
