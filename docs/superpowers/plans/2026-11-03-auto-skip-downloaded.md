# Auto Skip Downloaded Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a default-off, evidence-gated Auto Skip Downloaded menu option that advances a verified locally playing Spotify music track only after existing-file metadata processing completes.

**Architecture:** The existing-file worker owns the decision because it already has verified recording identity, completeness, ownership, and metadata-update results. It publishes a bounded generation-bound command to the existing Spotify collector; the collector revalidates local playback and exact URI before calling Spotify Player Next once.

**Tech Stack:** C++17, Win32/CEF, JavaScript, existing Windows native test harness.

**Spec:** `docs/superpowers/specs/2026-11-03-auto-skip-downloaded-design.md`

## Global Constraints

- Default off; missing or invalid `AutoSkipDownloaded` remains off.
- Music only; podcasts never auto-skip.
- No Windows media controls and no timer-based decisions.
- Unknown, stale, incomplete, unowned, mismatched, failed-update, remote, or paused evidence fails closed.
- Run only focused tests for files and paths changed.

## Review Focus

- A same-title or replayed track with a different Spotify URI/instance must not consume a stale request.
- A current FLAC that is a verified upgrade over saved audio must continue playing.
- Delayed quality evidence must retry without issuing duplicate requests for one playback instance.
- Metadata update failure must never advance playback.
- Feature disable or settings change between native decision and renderer dispatch must invalidate the request.

---

### Task 1: Persisted toggle and menu

**Files:**
- Modify: `native/history_settings.h`
- Modify: `native/history_settings.cpp`
- Modify: `native/to_disk_menu.cpp`
- Modify: `SpotifyHistory.ini`
- Test: `tests/windows_settings_test.cpp`
- Test: `tests/windows_menu_quality_test.cpp`

**Interfaces:**
- Produces: `Settings::auto_skip_downloaded` and `bool SetAutoSkipDownloaded(bool)`.
- Consumes: existing non-capture-affecting setting persistence and check-item menu patterns.

- [ ] Add failing focused settings assertions for default-off, explicit enable, persistence, and unchanged capture epoch.
- [ ] Run only the settings test executable and confirm the new assertions fail.
- [ ] Implement the setting, default INI line, persistence, and setter.
- [ ] Add failing menu assertions for placement below Lyrics, live toggle, and readable log label.
- [ ] Run only the menu test executable and confirm failure.
- [ ] Implement the menu item and run the two focused tests green.
- [ ] Commit the task.

### Task 2: Pure skip decision and command state

**Files:**
- Create: `native/auto_skip.h`
- Create: `native/auto_skip.cpp`
- Create: `tests/auto_skip_test.cpp`
- Modify: `native/playback_quality.h`
- Modify: `native/playback_quality.cpp`
- Modify: `build-native.sh`

**Interfaces:**
- Produces: `bool ShouldAutoSkipDownloaded(const PlaybackQualitySnapshot&, const Quality&)`, `void QueueAutoSkipRequest(std::string,unsigned)`, and `AutoSkipRequest ReadAutoSkipRequest()`.
- Consumes: `HigherQuality`, identity-bound `PlaybackQualitySnapshot`, and settings generation.

- [ ] Write failing table-driven tests covering unknown/stale quality, Ogg, FLAC upgrades, same/lower FLAC, and command generation replacement.
- [ ] Compile and run only `auto_skip_test`; confirm failure for missing interfaces.
- [ ] Implement the minimal pure decision and bounded latest-request state, including playback channels.
- [ ] Run `auto_skip_test` green and rebuild only the production DLL to prove linkage.
- [ ] Commit the task.

### Task 3: Post-metadata orchestration

**Files:**
- Modify: `native/audio_history.cpp`
- Test: `tests/windows_quality_observer_test.cpp`

**Interfaces:**
- Consumes: Task 1 setting and Task 2 decision/request API.
- Produces: one generation-bound auto-skip request per playback instance after successful metadata processing.

- [ ] Add failing focused cases for existing owned complete Ogg, upgrade-capable FLAC, unknown quality retry, metadata failure, podcast, and duplicate observations.
- [ ] Compile and run only `windows-quality-observer-test`; confirm the intended failures.
- [ ] Extend the filename-check key and payload with Spotify instance and current identity-bound quality.
- [ ] Queue a request only after metadata success and the pure quality decision; deduplicate by playback instance.
- [ ] Run only `windows-quality-observer-test` green.
- [ ] Commit the task.

### Task 4: Spotify command dispatch

**Files:**
- Modify: `native/collector_config.h`
- Modify: `native/metadata_bridge.cpp`
- Modify: `native/metadata_collector.js`
- Test: `tests/auto_skip_collector_test.js`

**Interfaces:**
- Consumes: Task 2 `AutoSkipRequest` and Task 1 current settings generation.
- Produces: one exact-URI, local-playback call to Spotify Player `next` for each request token.

- [ ] Add a failing JavaScript boundary test for success plus disabled, stale URI, duplicate token, remote, paused, and missing-command cases.
- [ ] Run only the JavaScript boundary test and confirm failure.
- [ ] Include only a current-generation request in collector options and implement fail-closed one-shot dispatch.
- [ ] Run the JavaScript test green, rebuild the production DLL, then rerun the four focused changed-path tests only.
- [ ] Commit the task.

### Task 5: Focused final review

**Files:**
- Review: all files changed since `a77fd32`

**Interfaces:**
- Consumes: all prior task outputs.
- Produces: review findings or a clean focused handoff.

- [ ] Build a review package from `a77fd32` to HEAD.
- [ ] Review lifecycle identity, quality comparison, metadata failure, request invalidation, and collector command capability.
- [ ] Fix any Critical/Important finding with a focused RED-to-GREEN regression.
- [ ] Run only the changed-path tests and production build once more.
