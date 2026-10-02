#include "playback_quality.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace history {
PlaybackQualityTracker::Candidate *PlaybackQualityTracker::Find(uintptr_t context,
                                                                PlaybackFormat format) {
  auto it = std::find_if(streams_.begin(), streams_.end(),
                         [&](const auto &c) { return c.context == context && c.format == format; });
  return it == streams_.end() ? nullptr : &*it;
}
PlaybackQualityTracker::Candidate &
PlaybackQualityTracker::Begin(uintptr_t context, PlaybackFormat format, double time) {
  streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
                                [&](const auto &c) { return c.context == context; }),
                 streams_.end());
  if (streams_.size() >= 16)
    streams_.pop_front();
  streams_.emplace_back();
  auto &c = streams_.back();
  c.context = context;
  c.format = format;
  c.born = time;
  return c;
}
void PlaybackQualityTracker::Media(const std::string &key, const std::wstring &title,
                                   double position, double duration, double time, bool playing) {
  if (key.empty() || title.empty() || !std::isfinite(position) || !std::isfinite(duration) ||
      !std::isfinite(time) || position < 0 || duration <= 0 || duration > 86400) {
    identity_.clear();
    title_.clear();
    playing_ = false;
    return;
  }
  const bool same = identity_ == key && std::fabs(duration_ - duration) <= 1.0;
  if (!same) {
    // A title change completes a separate SMTC timeline/title update. The prior
    // track's replay floor must not discard a next-track decoder buffered earlier.
    generation_floor_ = -1;
    settle_until_ = 0;
  }
  const double elapsed = time - last_time_;
  const double expected = last_position_ + (playing_ ? std::max(0.0, elapsed) : 0);
  const bool discontinuity = same && (elapsed < 0 || position < last_position_ - 1.5 ||
                                     position > expected + 3);
  if (discontinuity) {
    // Retire old decoder instances. New callbacks may precede this SMTC poll,
    // but instances already present at the previous poll cannot prove a restart.
    generation_floor_ = last_time_;
    settle_until_ = time + 2; // SMTC title and timeline can update separately.
  }
  if (!same || discontinuity ||
      (playing && !playing_ && Snapshot().format == PlaybackFormat::Unknown)) {
    start_ = time - std::min(position, duration);
    arrival_ = time;
  }
  last_position_ = position;
  last_time_ = time;
  playing_ = playing;
  identity_ = key;
  title_ = title.substr(0, 190);
  duration_ = duration;
}
void PlaybackQualityTracker::Ogg(uintptr_t context, const uint8_t *bytes, size_t length,
                                 double time) {
  if (!bytes || length < 27 || !std::isfinite(time))
    return;
  auto c = Find(context, PlaybackFormat::Ogg);
  if (!c || (bytes[5] & 2))
    c = &Begin(context, PlaybackFormat::Ogg, time);
  auto result = c->ogg.Push(bytes, length);
  if (result == Result::Invalid || result == Result::Ignore ||
      length > (uint64_t(1) << 40) - c->bytes) {
    c->broken = true;
    return;
  }
  c->bytes += length;
  c->metadata = c->ogg.rate != 0;
  c->complete = result == Result::Complete;
}
void PlaybackQualityTracker::FlacBegin(uintptr_t context, double time) {
  if (std::isfinite(time))
    Begin(context, PlaybackFormat::Flac, time);
}
void PlaybackQualityTracker::FlacBytes(uintptr_t context, const uint8_t *bytes, size_t length,
                                       size_t encoded_bytes) {
  auto c = Find(context, PlaybackFormat::Flac);
  if (!c || c->broken)
    return;
  if (length > encoded_bytes || (length && !bytes) ||
      encoded_bytes > (uint64_t(1) << 40) - c->bytes) {
    c->broken = true;
    return;
  }
  c->bytes += encoded_bytes;
  const auto n = std::min(length, c->prefix.size() - c->prefix_size);
  if (n) memcpy(c->prefix.data() + c->prefix_size, bytes, n);
  c->prefix_size += n;
  if (c->metadata) return;
  const auto status = ParseFlacStreamInfo(c->prefix.data(), c->prefix_size, c->flac);
  if (status == FlacParse::Invalid) c->broken = true;
  else if (status == FlacParse::Valid) c->metadata = true;
}
void PlaybackQualityTracker::FlacFrame(uintptr_t context, const uint8_t *bytes, size_t length) {
  auto c = Find(context, PlaybackFormat::Flac);
  if (!c || c->broken)
    return;
  if (!c->metadata || !bytes || length != 32) {
    c->broken = true;
    return;
  }
  uint32_t frame[6];
  uint64_t number;
  memcpy(frame, bytes, 24);
  memcpy(&number, bytes + 24, 8);
  if (frame[5] == 0)
    number = uint32_t(number);
  if (frame[1] != c->flac.rate || frame[2] != c->flac.channels || frame[4] != c->flac.bits ||
      !frame[0] || frame[0] > c->flac.max_block || frame[5] > 1) {
    c->broken = true;
    return;
  }
  if (!c->coverage_valid) return;
  if (!c->coverage.Frame(c->flac, frame[0], frame[1], frame[2], frame[4], frame[5], number)) {
    // Seeking can skip decoded frames without changing the validated header.
    // Preserve codec/rate; the compressed-byte average is no longer measurable.
    c->coverage_valid = false;
    c->complete = false;
    return;
  }
  c->complete = c->coverage.Complete(c->flac);
}
void PlaybackQualityTracker::FlacEnd(uintptr_t context, bool failed) {
  auto c = Find(context, PlaybackFormat::Flac);
  if (c && failed) c->broken = true;
  else if (c && !c->complete) c->coverage_valid = false;
}
void PlaybackQualityTracker::ClearStreams() { streams_.clear(); }
PlaybackLevel ParsePlaybackLevel(const std::string &value) {
  if(value == "low") return PlaybackLevel::Low;
  if(value == "normal") return PlaybackLevel::Normal;
  if(value == "high") return PlaybackLevel::High;
  if(value == "very_high") return PlaybackLevel::VeryHigh;
  if(value == "lossless" || value == "hifi") return PlaybackLevel::Lossless;
  if(value == "lossless_24") return PlaybackLevel::Lossless24;
  return PlaybackLevel::Unknown;
}
PlaybackQualitySnapshot PlaybackQualityTracker::Snapshot(const std::string &client_quality) const {
  PlaybackQualitySnapshot out;
  out.identity = PlaybackIdentity(identity_);
  std::copy(title_.begin(), title_.end(), out.title.begin());
  if (identity_.empty() || last_time_ < settle_until_)
    return out;
  out.level = ParsePlaybackLevel(client_quality);
  const Candidate *selected = nullptr;
  for (const auto &c : streams_) {
    if (c.broken || !c.metadata || c.born <= generation_floor_ ||
        c.born < start_ - 20 || c.born > arrival_ + 3)
      continue;
    double length = c.format == PlaybackFormat::Flac && c.flac.rate
                        ? double(c.flac.total_samples) / c.flac.rate
                        : (c.complete ? c.ogg.Duration() : 0);
    // A Vorbis BOS has no duration or track identity; read-ahead alone is not evidence.
    if (length <= 0 || std::fabs(length - duration_) > 1.0)
      continue;
    if (selected)
      return out; // Read-ahead and same-duration streams must not be guessed.
    selected = &c;
  }
  if (!selected)
    return out;
  const auto &c = *selected;
  // A current client quality that contradicts a decoder is a reason to wait,
  // not to assign a read-ahead decoder's codec to that song.
  const bool lossless = out.level == PlaybackLevel::Lossless || out.level == PlaybackLevel::Lossless24;
  if (out.level != PlaybackLevel::Unknown &&
      ((c.format == PlaybackFormat::Flac && !lossless) || (c.format == PlaybackFormat::Ogg && lossless)))
    return out;
  out.format = c.format;
  out.rate = c.format == PlaybackFormat::Flac ? c.flac.rate : c.ogg.rate;
  if (c.format == PlaybackFormat::Flac) {
    out.bits = c.flac.bits;
    out.level = c.flac.bits == 24 ? PlaybackLevel::Lossless24 : PlaybackLevel::Lossless;
  } else if (out.level == PlaybackLevel::Unknown) {
    out.level = c.ogg.bitrate == 320000 ? PlaybackLevel::VeryHigh :
                c.ogg.bitrate == 160000 ? PlaybackLevel::High :
                c.ogg.bitrate == 96000 ? PlaybackLevel::Normal : PlaybackLevel::Lossy;
  }
  if (c.complete) {
    const double length = c.format == PlaybackFormat::Flac
                              ? double(c.flac.total_samples) / c.flac.rate : c.ogg.Duration();
    out.bitrate = double(c.bytes) * 8 / length;
  }
  // Never divide read-ahead bytes by partial decoded coverage.
  return out;
}
uint64_t PlaybackIdentity(const std::string &key) {
  if (key.empty()) return 0;
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char byte : key) { hash ^= byte; hash *= 1099511628211ULL; }
  return hash;
}
std::array<std::wstring, 4> PlaybackQualityLabels(const PlaybackQualitySnapshot &q) {
  std::wstring title(q.title.begin(), std::find(q.title.begin(), q.title.end(), L'\0'));
  if (title.size() > 96) {
    title.resize(95);
    if (title.back() >= 0xd800 && title.back() <= 0xdbff)
      title.pop_back();
    title += L'\u2026';
  }
  std::wstring safe;
  for (wchar_t c : title) {
    if (c < L' ' || c == 127)
      c = L' ';
    safe += c;
    if (c == L'&')
      safe += c;
  }
  const wchar_t *level = L"Unavailable";
  switch(q.level) {
    case PlaybackLevel::Low: level=L"Low";break;
    case PlaybackLevel::Normal: level=L"Normal";break;
    case PlaybackLevel::High: level=L"High";break;
    case PlaybackLevel::VeryHigh: level=L"Very high";break;
    case PlaybackLevel::Lossless: level=L"Lossless";break;
    case PlaybackLevel::Lossless24: level=L"Lossless (24-bit)";break;
    case PlaybackLevel::Lossy: level=L"Lossy";break;
    default:break;
  }
  std::array<std::wstring, 4> rows = {L"Song: " + (safe.empty() ? L"Unavailable" : safe),
                                      std::wstring(L"Quality: ")+level, L"Format: Unavailable",
                                      L"Sample rate: Unavailable"};
  if (q.format == PlaybackFormat::Unknown) return rows;
  rows[2] = q.format == PlaybackFormat::Flac ? L"Format: FLAC" : L"Format: Ogg";
  if (q.rate) {
    auto number = std::to_wstring(q.rate);
    for (int p = int(number.size()) - 3; p > 0; p -= 3) number.insert(size_t(p), L",");
    rows[3] = L"Sample rate: " + number + L" Hz";
    if(q.bits) rows[3] += L" / " + std::to_wstring(q.bits) + L"-bit";
  }
  return rows;
}
} // namespace history
