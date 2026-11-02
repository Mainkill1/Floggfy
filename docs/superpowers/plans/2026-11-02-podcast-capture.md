# Podcast Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Save fully listened Spotify episodes as original-byte MP3 files under a dedicated Podcasts tree behind a default-off To Disk toggle.

**Architecture:** Classify MP3 starts in the shared decoded dispatcher, queue consumed encoded spans to the existing worker, and bind each complete MP3 context to an episode playback generation with the ownership tracker. A focused MP3 module validates source frames and builds/inspects owned ID3v2.4 files for the existing atomic publisher.

**Tech Stack:** C++17, Win32, MinHook, CEF menu integration, existing compressed buffer/publication infrastructure.

**Spec:** `docs/superpowers/specs/2026-11-02-podcast-capture-design.md`

## Global Constraints

- No PCM recording, transcoding, new network request, or external runtime.
- `Podcasts=0` by default; Downloads remains the master switch.
- Only exact `spotify:episode:` playback may publish into `Podcasts\<Show>`.
- Any malformed stream, queue loss, seek/skip, overlap, or ownership uncertainty fails closed.
- Source MP3 frame bytes must be unchanged after tagging.

## Review Focus

- ID3v2.3/v2.4 source headers and ID3-free MP3 starts both retain every audio frame.
- VBR frames, MPEG-1/2/2.5 rates, padding, CRC flags, and mono/stereo headers produce correct duration.
- Truncation, junk between frames, reserved bitrate/rate/version/layer values, and trailing junk are rejected.
- A new decoder that overlaps the episode cannot be made unique by later cleanup.
- Repeated playback of the same episode uses playback generation, not URI/title alone.

---

### Task 1: MP3 validation and owned ID3 files

**Files:**
- Create: `native/mp3_stream.h`
- Create: `native/mp3_stream.cpp`
- Create: `tests/mp3_stream_test.cpp`
- Modify: `native/library_layout.h`
- Modify: `native/recording_file.cpp`
- Modify: build/test scripts that enumerate native sources

**Interfaces:**
- Produces: `InspectMp3Source`, `MakeOwnedMp3`, and `InspectOwnedMp3` with parsed rate, channels, samples, frame count, audio byte range, and tags.

- [ ] Write literal MP3/ID3 fixtures covering the Review Focus cases and assert exact retained audio bytes.
- [ ] Run the focused test and verify it fails because the MP3 interfaces do not exist.
- [ ] Implement strict bounded parsing and ID3v2.4 construction/inspection; add `Codec::Mp3`.
- [ ] Run the focused test and portable suite; expect all tests to pass.

### Task 2: Podcast setting, menu, metadata, and path

**Files:**
- Modify: `native/history_settings.h`
- Modify: `native/history_settings.cpp`
- Modify: `native/to_disk_menu.cpp`
- Modify: `native/audio_history.cpp`
- Modify: `native/metadata_collector.js`
- Test: `tests/windows_settings_test.cpp`
- Test: `tests/windows_settings_unicode_test.cpp`
- Test: `tests/windows_menu_quality_test.cpp`
- Test: collector playback/metadata tests and library layout tests

**Interfaces:**
- Produces: `Settings::podcasts`, `SetPodcasts(bool)`, `PodcastsEnabled()`, and episode-to-`MediaKind::Podcast` catalog mapping.

- [ ] Add failing tests for default off, live/persisted toggle, visible menu checkbox, episode footer metadata, and `Podcasts\Show\Episode.mp3` routing.
- [ ] Run the focused tests and verify the missing setting/menu/path failures.
- [ ] Implement the setting and menu command, emit bounded episode metadata without music enrichment, and classify episode catalogs.
- [ ] Run the focused tests; expect all to pass.

### Task 3: Decoder-span MP3 capture and ownership

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `native/decoder_ownership.h`
- Test: `tests/windows_playback_diagnostics_test.cpp`
- Test: `tests/decoder_ownership_test.cpp`

**Interfaces:**
- Consumes: Task 1 MP3 parser and Task 2 `PodcastsEnabled()`.
- Produces: bounded MP3 decoder events, complete MP3 `Capture` objects, and exact episode publication jobs.

- [ ] Add failing tests for ID3 classification, chunked capture, exact playback binding, same-episode repeats, overlap rejection, queue loss, skip/seek rejection, and final revalidation.
- [ ] Run focused diagnostics and verify the intended capture/reconciliation failures.
- [ ] Replace the temporary signature probe with bounded MP3 queuing, validation, ownership, and publication integration.
- [ ] Run focused and full native/Windows diagnostics; expect all to pass.

### Task 4: Documentation and live client verification

**Files:**
- Modify: `AUDIO-HISTORY.md`
- Modify: release/default INI if separately tracked

- [ ] Document the Podcasts toggle, MP3-only current support, folder layout, original-byte guarantee, and fail-closed behavior.
- [ ] Build/install the DLL and test one ID3v2.3 and one ID3v2.4 episode at 1x/10x in an isolated save root.
- [ ] Independently verify frame continuity/duration, source-frame byte identity, tags, folder path, duplicate behavior, and no lingering test processes.
- [ ] Restore the user's original INI while keeping the validated DLL installed.

