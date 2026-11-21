#pragma once
#include "../media/playback_quality.h"
#include <algorithm>
#include <string_view>

namespace history {
inline constexpr std::array<std::wstring_view, 4> kPlaybackLabelPrefixes{
    L"Song: ", L"Quality: ", L"Format: ", L"Sample rate: "};

inline std::wstring MenuTitle(std::wstring_view title, wchar_t last_control) {
  const bool truncated = title.size() > 96;
  if (truncated) {
    title = title.substr(0, 95);
    if (title.back() >= 0xd800 && title.back() <= 0xdbff) title.remove_suffix(1);
  }
  std::wstring safe;
  safe.reserve(title.size() + std::count(title.begin(), title.end(), L'&') + truncated);
  for (wchar_t c : title) {
    if (c < L' ' || (c >= 0x7f && c <= last_control)) c = L' ';
    safe += c;
    if (c == L'&') safe += c;
  }
  if (truncated) safe += L'\u2026';
  return safe;
}
inline std::wstring PlaybackQualityLabel(const PlaybackQualitySnapshot &q, size_t row) {
  std::wstring label(kPlaybackLabelPrefixes.at(row));
  switch (row) {
    case 0: {
      const auto end = std::find(q.title.begin(), q.title.end(), L'\0');
      if (end != q.title.begin())
        return label + MenuTitle({q.title.data(), size_t(end - q.title.begin())}, 0x7f);
      break;
    }
    case 1:
      switch (q.level) {
        case PlaybackLevel::Low: return label + L"Low";
        case PlaybackLevel::Normal: return label + L"Normal";
        case PlaybackLevel::High: return label + L"High";
        case PlaybackLevel::VeryHigh: return label + L"Very high";
        case PlaybackLevel::Lossless: return label + L"Lossless";
        case PlaybackLevel::Lossless24: return label + L"Lossless (24-bit)";
        case PlaybackLevel::Lossy: return label + L"Lossy";
        default: break;
      }
      break;
    case 2:
      switch (q.format) {
        case PlaybackFormat::Flac: return label + L"FLAC";
        case PlaybackFormat::Ogg: return label + L"Ogg";
        case PlaybackFormat::Mp3: return label + L"MP3";
        case PlaybackFormat::Aac: return label + L"AAC";
        default: break;
      }
      break;
    case 3:
      if (q.format != PlaybackFormat::Unknown && q.rate) {
        auto number = std::to_wstring(q.rate);
        for (int p = int(number.size()) - 3; p > 0; p -= 3) number.insert(size_t(p), L",");
        label += number + L" Hz";
        if (q.bits) label += L" / " + std::to_wstring(q.bits) + L"-bit";
        return label;
      }
  }
  return label + L"Unavailable";
}
inline std::array<std::wstring, 4> PlaybackQualityLabels(const PlaybackQualitySnapshot &live,
                                                 const PlaybackQualitySnapshot *song = nullptr) {
  return {PlaybackQualityLabel(song ? *song : live, 0), PlaybackQualityLabel(live, 1),
          PlaybackQualityLabel(live, 2), PlaybackQualityLabel(live, 3)};
}
} // namespace history
