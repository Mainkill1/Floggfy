# FLAC overlap and repair incident

## Purpose

This page records a live Spotify incident in which Floggfy reported a failed FLAC
capture while the selected song continued playing normally. It separates what the
logs prove from what they merely suggest, explains why the second stream could not
stand alone, and outlines possible improvements without weakening file-integrity
requirements.

The incident is useful because it exposes three different outcomes that should not
be collapsed into one message:

1. a compressed decoder stream was unusable as a complete recording;
2. another capture for the song may still have been healthy; and
3. an overlapping stream might contain useful repair frames even though it is not
   independently publishable.

## Test context

| Item | Observed value |
| --- | --- |
| Local time | 2026-11-03 13:56:22 through 13:56:56, UTC-08:00 |
| Runtime | Live Windows Spotify process |
| Spotify client | 1.3.8.614, Windows x64 |
| Floggfy DLL SHA-256 | `9a0aec2fb4832bd9c5d22dc52a8d2cf2e0055e89295680bad3b7e35d56d8d643` |
| Source revision under investigation | `f70b0c749734e20fbe7ac4a4c98f78397c01044f` plus isolated diagnostic work |
| Capture speed | 1x |
| Track | AC/DC - Back In Black |
| Codec | FLAC, 44,100 Hz, stereo, 24-bit |

The runtime logs are local test artifacts and are not committed. The fields below
are reproduced so the reasoning remains reviewable if those bounded logs rotate.

## Event timeline

| Sequence | Evidence | Meaning |
| ---: | --- | --- |
| 12-13 | Capture 1 begins at 13:56:22.764; STREAMINFO declares 11,289,605 samples. | A primary FLAC capture starts near the beginning of the song. |
| 18-20 | Owner 1 produces decoded output. | Capture 1's decoder is active. |
| 30 | Spotify reports Ready at `0.084 / 256.000` seconds. | The track timeline begins near zero. |
| 33-36 | The player-ID bridge matches owner generation 1 and binds capture 1 to playback 2/listen generation 1. | Capture 1 has direct playback ownership evidence. |
| 38-41 | Capture 2 begins 12.922 seconds later with the same reported FLAC format, declared samples, and metadata size. | Spotify creates or reinitializes another decoder stream during the same track. |
| 42 | Capture 2's first observed frame has sample number `438272`, while Floggfy expects sample zero. | Capture 2 is missing its first 438,272 samples and cannot be a complete standalone FLAC. |
| 43-44 | Capture 2 is aborted; its 32,768 cached bytes are released. | Current code discards the auxiliary capture immediately. |
| 45 | Owner 2 produces decoded output after the abort event has already been consumed. | The rejected stream is a real active decoder, not random bytes. |
| 47-48 | An app transition is reported as natural and quarantined for validation. | The client emitted a transition-like stage even though the track did not actually change. |
| 50-53 | Spotify reports Ready at `12.793 / 256.000`, `track_changed=0`; the bridge matches owner generation 2. | The same playing track is now associated with the second decoder owner. |
| 55 | Playback pauses later at 33.089 seconds. | Playback survived the discarded auxiliary capture. |

The user-facing activity log emitted both `failed incomplete cache | unknown track`
and `failed FLAC decoded-frame gap or format mismatch; stream discarded`. The
second statement is correct about capture 2 in isolation. The first overstates the
song-level outcome because capture 1 had not been shown to fail.

## What the numbers prove

The second stream's first FLAC frame starts at:

```text
438,272 samples / 44,100 samples per second = 9.938 seconds
```

The next Ready observation places audible playback at 12.793 seconds:

```text
12.793 - 9.938 = 2.855 seconds
```

Capture 2 therefore began about 2.86 seconds behind the reported playback
position. It skipped the first 9.94 seconds **within its own compressed stream**.
It did not cause Spotify playback to skip 9.94 seconds.

This proves overlap with the playback timeline. It does **not** prove the exact
compressed-frame overlap with capture 1, because the log did not record capture
1's covered-sample endpoint when capture 2 arrived. Capture 1 started at sample
zero and remained bound during the incident window, so overlap between the two
captures is likely, but that remains an inference.

The following facts are also not established by the current evidence:

- whether capture 1 eventually received every declared sample;
- whether capture 2 would have continued to the song's final sample;
- whether both streams contain byte-identical FLAC frames;
- whether they decode to exactly the same PCM; or
- whether capture 2 could fill a real future hole in capture 1.

## Current behavior

FLAC coverage is intentionally strict. Every capture begins with an expected sample
of zero, and every decoded frame must start at the next expected sample. A first
frame at sample 438,272 fails the `sample-position` guard. The active capture is
then aborted and removed in
[`ProcessFlacCapture`](../../native/audio_history.cpp).

This protects against publishing a file with a missing beginning. The existing
sample-zero restart path handles the opposite decoder behavior: a later frame at
sample zero can restart one capture cleanly. It does not merge two capture objects.

The parser currently retains rate, channel count, bit depth, block sizes, and total
samples in [`FlacInfo`](../../native/flac_history_core.h). It does not retain the
STREAMINFO MD5 field. Equal format and duration are therefore not enough to prove
that two streams contain the same audio.

## Could the captures be combined?

In principle, yes, but only as validated frame-level repair. Capture 2 cannot be
prepended to make a complete file because its beginning is absent. It could only
supply a suffix or fill a later missing range if all of the following were true:

1. Both captures are tied to the same playback identity and source generation.
2. Their complete STREAMINFO parameters agree, including a usable PCM MD5 or an
   equally strong source identity.
3. Every candidate FLAC frame passes its header and CRC checks.
4. Capture 1 reaches the beginning of a frame retained from capture 2 with no
   missing samples.
5. Frames already covered by capture 1 are discarded as duplicates.
6. The appended frames continue monotonically to the exact declared total sample
   count.
7. The assembled file passes a complete independent parse and, when present, its
   STREAMINFO PCM MD5 verification before publication.

If capture 1 ended before sample 438,272, capture 2 could not repair the gap before
that point. If capture 1 passed sample 438,272, capture 2's overlapping frames could
be ignored until an exact common frame boundary. If no exact boundary appears, the
captures must not be spliced.

The bytes from this incident cannot be repaired after the fact: capture 2 was
released immediately, and the diagnostic log records frame metadata rather than
the media payload.

## Improvement approaches

### 1. Classify without merging — recommended first step

Keep the strict standalone capture rule, but inspect related captures before
emitting a song-level failure. When a midstream FLAC appears while another capture
is already bound to the same unchanged playback, classify it as an auxiliary
continuation and discard it without claiming that the song failed.

This is the smallest and safest correction. It removes the false alarm while
preserving the complete-from-sample-zero publication requirement.

### 2. Retain bounded overlap evidence

Before discarding an auxiliary stream, retain a small bounded record containing:

- playback identity and listen generation;
- decoder/capture generation and producer sequence;
- full STREAMINFO or a stable digest, including its MD5 field;
- incoming first and last sample positions;
- the primary capture's covered range at the same decision point; and
- whether either capture later completed, aborted, or lost events.

This would prove whether live cases are overlap, forward gaps, or unrelated
streams. It avoids retaining audio indefinitely and provides the evidence needed
before designing repair behavior.

### 3. Bounded frame-range repair

Treat compatible auxiliary streams as temporary frame-range candidates rather than
complete recordings. Index validated frames by sample interval, prefer the bound
primary capture, and consult auxiliary ranges only if the primary later proves
incomplete. Never let a retained auxiliary candidate create completeness; it may
only supply exact missing frame ranges. Discard all candidates after a bounded
lifecycle or once the primary validates completely.

This could recover a real decoder restart, but it adds memory pressure, ownership
rules, and another assembly path. It should not be implemented until approach 2
shows repeated repairable gaps in live evidence.

## Recommended diagnostic change

When a FLAC frame fails only because its first sample is nonzero, log one structured
classification record before releasing it:

```text
auxiliary-flac capture=2 playback=2 incoming=[438272,...)
primary=1 primary_covered=<samples> relation=overlap|gap|unknown
streaminfo_match=<level> action=discard|retain-for-repair
```

The relation must be derived from sample intervals, not elapsed-time guesses:

- `overlap`: primary coverage is greater than the incoming first sample;
- `contiguous`: primary coverage equals the incoming first sample;
- `gap`: primary coverage is less than the incoming first sample; and
- `unknown`: no uniquely bound primary capture or missing source identity.

The normal activity log should report a track failure only if the track's chosen
primary capture later fails or no valid complete capture survives. An auxiliary
discard belongs in diagnostics unless it changes that final outcome.

## Regression tests before repair ships

- Primary capture remains complete while an overlapping midstream decoder appears;
  no track failure is emitted.
- Primary stops exactly at the auxiliary first-frame boundary; repair succeeds and
  final validation passes.
- Primary stops before the auxiliary first frame; the gap remains unrepaired and
  publication is refused.
- Same format and duration but different STREAMINFO identity; candidates are never
  combined.
- Repeated same song with different playback generations; frames are never crossed
  between listens.
- Decoder pointer reuse and reordered worker events; lifecycle generations prevent
  cross-capture assembly.
- Corrupt frame, bad CRC, queue loss, or bounded-storage eviction; repair is refused
  without weakening the primary integrity result.
- A repaired file is decoded or independently inspected to confirm exact declared
  sample coverage and PCM MD5 where available.

## Decision

For this incident, the defensible classification is:

> Capture 2 was an incomplete midstream FLAC associated with the unchanged playing
> track. Discarding it as a standalone recording was correct. Reporting the song as
> failed was premature. Exact overlap with capture 1 and repairability were not
> recorded, so combining the streams would have been a guess.

The next improvement should capture the missing interval and STREAMINFO identity
evidence and correct the user-facing outcome. Generalized splicing should remain a
later, evidence-driven feature rather than an automatic response to decoder
reinitialization.
