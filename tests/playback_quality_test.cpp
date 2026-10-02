#include "../native/playback_quality.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace history;
static void Put(std::vector<uint8_t> &b, size_t p, uint64_t value, unsigned count) {
  for (unsigned i = 0; i < count; i++)
    b[p + i] = uint8_t(value >> (8 * i));
}
static std::vector<uint8_t> OggPage(unsigned sequence, unsigned flags, uint64_t samples,
                                    bool identification = false) {
  std::vector<uint8_t> b(58);
  memcpy(b.data(), "OggS", 4);
  b[5] = uint8_t(flags);
  Put(b, 6, samples, 8);
  Put(b, 14, 42, 4);
  Put(b, 18, sequence, 4);
  b[26] = 1;
  b[27] = 30;
  if (identification) {
    memcpy(b.data() + 28, "\x01vorbis", 7);
    b[39] = 2;
    Put(b, 40, 44100, 4);
    Put(b, 48, 320000, 4);
  }
  uint32_t crc = 0;
  for (auto byte : b) {
    crc ^= uint32_t(byte) << 24;
    for (unsigned i = 0; i < 8; i++)
      crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
  }
  Put(b, 22, crc, 4);
  return b;
}
static std::vector<uint8_t> FlacPrefix() {
  std::vector<uint8_t> b = {'f', 'L', 'a', 'C', 0, 0, 0, 34};
  b.resize(42);
  b[8] = b[10] = 0xbb;
  b[9] = b[11] = 0x80; // 48000 samples per frame
  uint64_t packed = (uint64_t(48000) << 44) | (uint64_t(1) << 41) | (uint64_t(23) << 36) | 9600000;
  for (unsigned i = 0; i < 8; i++)
    b[18 + i] = uint8_t(packed >> (56 - i * 8));
  return b;
}
static void Frame(PlaybackQualityTracker &tracker, unsigned index) {
  std::vector<uint8_t> b(32);
  Put(b, 0, 48000, 4);
  Put(b, 4, 48000, 4);
  Put(b, 8, 2, 4);
  Put(b, 16, 24, 4);
  Put(b, 20, 1, 4);
  Put(b, 24, uint64_t(index) * 48000, 8);
  tracker.FlacFrame(2, b.data(), b.size());
}
int main() {
  PlaybackQualityTracker tracker;
  tracker.Media("song", L"Song", 0, 200, 100);
  auto first = OggPage(0, 2, 0, true);
  tracker.Ogg(1, first.data(), first.size(), 100);
  auto q = tracker.Snapshot();
  assert(q.format == PlaybackFormat::Unknown); // BOS alone has no duration/identity evidence.
  auto end = OggPage(1, 4, 8820000);
  tracker.Ogg(1, end.data(), end.size(), 101);
  q = tracker.Snapshot();
  assert(q.level == PlaybackLevel::VeryHigh && std::fabs(q.bitrate - 4.64) < 0.0001);
  auto next_end = OggPage(1, 4, 7938000);
  tracker.Ogg(3, first.data(), first.size(), 110);
  tracker.Ogg(3, next_end.data(), next_end.size(), 111);
  assert(tracker.Snapshot()
             .format == PlaybackFormat::Ogg); // Read-ahead for the next track cannot replace the current source.
  tracker.Media("next", L"Next", 0, 180, 110);
  assert(tracker.Snapshot().format == PlaybackFormat::Ogg);
  tracker.Ogg(4, first.data(), first.size(), 111);
  tracker.Ogg(4, next_end.data(), next_end.size(), 112);
  assert(tracker.Snapshot().format ==
         PlaybackFormat::Unknown); // Same-duration candidates must remain ambiguous.
  tracker.ClearStreams();
  assert(tracker.Snapshot().format == PlaybackFormat::Unknown);
  tracker.Media("flac", L"Lossless", 0, 200, 200);
  tracker.FlacBegin(2, 200);
  auto header = FlacPrefix();
  tracker.FlacBytes(2, header.data(), 5, 5);
  tracker.FlacBytes(2, header.data() + 5, 37, 99995);
  q = tracker.Snapshot();
  assert(q.format == PlaybackFormat::Flac && q.rate == 48000 && q.bitrate == 0);
  for (unsigned i = 0; i < 10; i++)
    Frame(tracker, i);
  q = tracker.Snapshot();
  assert(q.bitrate == 0); // Read-ahead is not decoded-byte progress.
  for (unsigned i = 10; i < 200; i++)
    Frame(tracker, i);
  q = tracker.Snapshot();
  assert(q.bitrate == 4000 && q.level == PlaybackLevel::Lossless24);
  tracker.Media("other", L"Other", 0, 180, 201);
  assert(tracker.Snapshot().format == PlaybackFormat::Unknown);
  tracker.Media("flac", L"Lossless", 0, 200, 200);
  tracker.FlacEnd(2, true);
  assert(tracker.Snapshot().format == PlaybackFormat::Unknown);
  tracker.ClearStreams();
  tracker.Media("resumed", L"Resumed", 50, 200, 300, false);
  tracker.FlacBegin(2, 400);
  tracker.FlacBytes(2, header.data(), header.size(), 100000);
  tracker.Media("resumed", L"Resumed", 50, 200, 400, true);
  assert(tracker.Snapshot().format == PlaybackFormat::Flac);
  // Observation starts midtrack, with only an unrelated next-track BOS available.
  PlaybackQualityTracker midtrack;
  midtrack.Media("a", L"Current", 50, 200, 100);
  midtrack.Ogg(8, first.data(), first.size(), 101);
  assert(midtrack.Snapshot().format == PlaybackFormat::Unknown);
  midtrack.Ogg(8, next_end.data(), next_end.size(), 102);
  assert(midtrack.Snapshot().format == PlaybackFormat::Unknown);
  // Same-key replay must exclude the previous codec even before the new decoder arrives.
  PlaybackQualityTracker replay;
  replay.Media("a", L"Current", 0, 200, 100);
  replay.Ogg(1, first.data(), first.size(), 100);
  replay.Ogg(1, end.data(), end.size(), 101);
  replay.Media("a", L"Current", 199, 200, 299);
  assert(replay.Snapshot().format == PlaybackFormat::Ogg);
  replay.Media("a", L"Current", 0, 200, 300);
  assert(replay.Snapshot().format == PlaybackFormat::Unknown);
  replay.FlacBegin(2, 300);
  replay.FlacBytes(2, header.data(), header.size(), 100000);
  replay.Media("a", L"Current", 3, 200, 303);
  assert(replay.Snapshot().format == PlaybackFormat::Flac);
  replay.Media("a", L"Current", 70, 200, 304); // Forward seek also retires the previous association.
  assert(replay.Snapshot().format == PlaybackFormat::Unknown);
  // Native next-track read-ahead can precede the old title's timeline reset.
  PlaybackQualityTracker transition;
  transition.Media("a", L"Old", 199, 200, 299);
  transition.Ogg(8, first.data(), first.size(), 298.2);
  transition.Ogg(8, next_end.data(), next_end.size(), 298.3);
  transition.Media("a", L"Old", 0, 200, 300); // Timeline updates before title.
  assert(transition.Snapshot().format==PlaybackFormat::Unknown);
  transition.Media("b", L"New", 1, 180, 301);
  assert(transition.Snapshot().format==PlaybackFormat::Ogg);
  // Client-reported current quality is usable before a decoder can be matched.
  PlaybackQualityTracker cached;
  cached.Media("a", L"Current", 0, 200, 500);
  q = cached.Snapshot("very_high");
  assert(q.level == PlaybackLevel::VeryHigh && q.format == PlaybackFormat::Unknown);
  cached.FlacBegin(2, 500);
  cached.FlacBytes(2, header.data(), header.size(), 100000);
  assert(cached.Snapshot("very_high").format == PlaybackFormat::Unknown); // Conflicting source.
  q = cached.Snapshot("lossless_24");
  assert(q.format == PlaybackFormat::Flac && q.bits == 24 && q.level == PlaybackLevel::Lossless24);
  assert(ParsePlaybackLevel("3") == PlaybackLevel::Unknown); // No guessed numeric enum mapping.
  assert(ParsePlaybackLevel("future") == PlaybackLevel::Unknown);
  PlaybackQualityTracker sought;
  sought.Media("seeked", L"Seeked", 20, 200, 600);
  sought.FlacBegin(2, 600);sought.FlacBytes(2, header.data(), header.size(), 100000);
  Frame(sought, 20); // Midtrack decoding does not invalidate valid codec/rate.
  q=sought.Snapshot();
  assert(q.format==PlaybackFormat::Flac && q.rate==48000 && q.bits==24 && q.bitrate==0);
  sought.FlacEnd(2,false);
  assert(sought.Snapshot().format==PlaybackFormat::Flac && sought.Snapshot().bitrate==0);
  sought.FlacEnd(2,true);assert(sought.Snapshot().format==PlaybackFormat::Unknown);
  q = {};
  const auto unknown = PlaybackQualityLabels(q);
  assert(unknown[3].find(L"Unavailable") != std::wstring::npos);
  q.format = PlaybackFormat::Ogg;
  q.rate = 44100;
  q.bitrate = 320000;
  q.level = PlaybackLevel::VeryHigh;
  std::copy_n(L"Song & Guest\n", 13, q.title.begin());
  auto labels = PlaybackQualityLabels(q);
  assert(labels[0].find(L"&&") != std::wstring::npos &&
         labels[0].find(L'\n') == std::wstring::npos);
  assert(labels[3].find(L"320 kbps") != std::wstring::npos &&
         labels[3].find(L"average") != std::wstring::npos);
  assert(labels[4].find(L"44,100 Hz") != std::wstring::npos &&
         labels[1].find(L"Very high") != std::wstring::npos);

  std::cout << "PASS: current-source association, ambiguity rejection, Ogg duration corroboration/actual rates, "
               "bounded FLAC prefix/read-ahead rejection, cached quality/codec consistency, replay/seek codec changes and menu labels\n";
}
