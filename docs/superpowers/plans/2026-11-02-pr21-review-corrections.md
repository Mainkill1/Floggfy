# PR #21 Review Corrections Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve the reproducible lifecycle, host-safety, podcast-integration, UI, and maintainability findings raised against PR #21 without replacing `v1.3-RC1`.

**Architecture:** Keep hooks as bounded event producers, make `DecoderOwnershipTracker` the single lifecycle correlator, and make publication consume explicit capability and completeness evidence. Consolidate format facts and parser walks where the review exposed drift, then remove confirmed unreachable interfaces and add a reproducible CI gate.

**Tech Stack:** C++17, Windows/CEF/MinHook, JavaScript collector, Bash test runners, GitHub Actions.

**Spec:** PR #21 review supplied by the user on 2026-11-02.

## Global Constraints

- Preserve conservative matching: uncertainty may skip a save but must not mislabel or overwrite audio.
- Never unwind an allocation failure through Spotify/CEF callbacks or terminate the host process.
- Preserve Windows-only playback fallback and 1x capture when optional decoded observation is unavailable.
- Keep podcast audio under `Podcasts/` and make saved MP3 episodes discoverable by the current-item folder action.
- Do not alter, replace, or retag `v1.3-RC1`.

## Review Focus

- Reused decoder addresses: delayed evidence must bind only the lifetime whose validated time interval contains it.
- Control lifetime: pause/resume acknowledgment must not poison a later natural transition, while seek/skip remains disqualifying.
- Capability combinations: discovered layout without an active decoded hook, and Windows observations without instance IDs, must still use a safe fallback policy.
- Owned MP3 integrity: frame-aligned truncation and allocation failure must remain recoverable and never become trusted complete output.
- Episode integration: one real collector payload must survive native parsing, cache lookup, tagging, publication, and destination lookup.

---

### Task 1: Ownership lifecycle recovery and delayed decoded evidence

**Files:**
- Modify: `native/decoder_ownership.cpp`
- Modify: `native/decoder_ownership.h`
- Test: `tests/decoder_ownership_test.cpp`

**Interfaces:**
- Consumes: capture begin/complete/abort, decoded activity intervals, playback instances.
- Produces: `OwnershipBinding` that retires conflicted current roles and accepts uniquely time-matched delayed evidence.

- [ ] Add a failing regression that conflicts and completes two captures, then binds a nonoverlapping capture without `Reset()`.
- [ ] Add a failing regression for begin → complete → decoded → playback, plus two nonoverlapping lifetimes reusing one owner address.
- [ ] Run the focused ownership test and verify both fail for the reviewed reasons.
- [ ] Retire conflicted/completed captures from current/pending roles without deleting their evidence; reconcile decoded activity against exactly one active or completed lifetime whose interval contains the observation.
- [ ] Run the focused test and commit the green task.

### Task 2: Playback control acknowledgment

**Files:**
- Modify: `native/metadata_collector.js`
- Test: `tests/collector_playback_test.js`
- Test: `tests/windows_playback_diagnostics_test.cpp`

**Interfaces:**
- Consumes: action events and playback state/instance observations.
- Produces: transition flags whose `controlled` evidence belongs only to the unacknowledged action and instance.

- [ ] Add a failing collector regression for pause → resume → sustained playback → natural completion.
- [ ] Add/extend the native transition regression so natural completion is accepted after acknowledged pause/resume while seek/skip remains rejected.
- [ ] Replace the global sticky boolean with bounded pending-control state tied to playback instance and acknowledged by the matching state change.
- [ ] Run collector and native transition tests and commit the green task.

### Task 3: Explicit ownership-observation capabilities and fallback policies

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `native/decoder_ownership.h`
- Modify: `native/decoder_ownership.cpp`
- Test: `tests/windows_playback_diagnostics_test.cpp`
- Test: `tests/hook_rollback_test.cpp`
- Test: `tests/hook_startup_batch_test.cpp`

**Interfaces:**
- Consumes: layout-discovery state, decoded-hook activation state, playback provider instance capability.
- Produces: capture ownership enforcement only when both decoded and playback-instance evidence are available.

- [ ] Add failing regressions for Windows-only media with a discovered owner, decoded-hook creation failure, and decoded-hook queue failure followed by successful removal.
- [ ] Model decoded observation availability separately from layout discovery; do not stamp owners when decoded observation is inactive.
- [ ] Record whether each completed listen has instance evidence and bypass only the ownership gate—not integrity/timing/duration gates—when the provider cannot supply it.
- [ ] Run hook and playback integration tests and commit the green task.

### Task 4: MP3 allocation containment, shared parsing, and exact completeness

**Files:**
- Modify: `native/mp3_stream.cpp`
- Modify: `native/mp3_stream.h`
- Modify: `native/recording_file.cpp`
- Modify: `native/recording_file.h`
- Modify: `native/existing_quality.cpp`
- Test: `tests/mp3_stream_test.cpp`
- Test: `tests/recording_file_test.cpp`
- Test: `tests/windows_publication_test.cpp`

**Interfaces:**
- Consumes: raw or segmented MP3 bytes and publisher-emitted `HISTORY_SAMPLES`.
- Produces: exception-contained inspection and an owned-but-incomplete recording result that equal-quality publication repairs in place.

- [ ] Add allocation-failure regressions covering text, field growth, artwork, and error creation; verify the caller survives and receives no trusted partial tags.
- [ ] Remove false `noexcept` contracts from allocating inspection or contain every allocation internally with nonallocating failure state.
- [ ] Replace duplicate raw/`CompressedBuffer` MPEG walks with one bounded byte-reader implementation.
- [ ] Add failing frame-boundary and in-frame truncation regressions using exact `HISTORY_SAMPLES` evidence.
- [ ] Require parsed samples to equal owned sample evidence; preserve owned identity on incompleteness so the real destination/publication path upgrades it.
- [ ] Run parser, recording, and Windows publication tests and commit the green task.

### Task 5: Episode enrichment contract

**Files:**
- Modify: `native/rich_metadata.cpp`
- Modify: `native/rich_metadata.h`
- Modify: `native/metadata_bridge.cpp`
- Test: `tests/metadata_collector_test.js`
- Test: `tests/rich_metadata_test.cpp`
- Test: `tests/cached_metadata_test.cpp`
- Test: `tests/metadata_update_test.cpp`

**Interfaces:**
- Consumes: collector episode payloads with `spotify:episode:` URI and `PUBLISHER`.
- Produces: validated `RichMetadata` records usable by cache lookup and final MP3 tagging.

- [ ] Add a failing native test using the exact episode payload shape emitted by the collector.
- [ ] Make the enrichment parser accept validated episode identity while retaining track requirements and field limits.
- [ ] Exercise the payload through parser, cache, and metadata fill/tagging.
- [ ] Run collector and metadata suites and commit the green task.

### Task 6: Shared format lookup, podcast destination, and live speed menu

**Files:**
- Create: `native/recording_formats.h`
- Modify: `native/song_destination.cpp`
- Modify: `native/to_disk_menu.cpp`
- Modify: `native/history_settings.cpp`
- Test: `tests/windows_song_destination_test.cpp`
- Test: `tests/windows_menu_quality_test.cpp`

**Interfaces:**
- Produces: one format descriptor table for extension/media-kind lookup and menu state refreshed from current settings/capability.

- [ ] Add failing destination tests for canonical, numbered, and collision-suffixed episode MP3 files.
- [ ] Add MP3 to request construction and every lookup/conversion path through the shared descriptor table.
- [ ] Add failing menu tests for a native fallback from accelerated speed to 1x and unavailable acceleration.
- [ ] Refresh speed checks/enabled states on each menu opening and report unsupported acceleration separately from persistence failure.
- [ ] Run destination/menu tests and commit the green task.

### Task 7: Remove confirmed dead paths and replace stale execution docs

**Files:**
- Delete: `native/lyrics_update.cpp`, `native/lyrics_update.h`, `native/module_pending.h`
- Modify: `native/flac_history_core.cpp`, `native/flac_history_core.h`
- Modify: `native/version_proxy.cpp`, `native/metadata_bridge.cpp`, `native/metadata_bridge.h`
- Modify: `native/to_disk_menu.cpp`, `native/to_disk_menu.h`, `native/media_session.cpp`, `native/media_session.h`
- Modify: `native/audio_history.cpp`, `native/playback_quality.cpp`, `native/playback_quality.h`
- Modify: `native/metadata_collector.js`, `native/existing_quality.*`, `native/file_publication.*`
- Modify: build/test scripts and affected tests
- Create: `docs/capture-invariants.md`
- Delete: superseded capture execution plans under `docs/superpowers/plans/` and `docs/superpowers/specs/`

**Interfaces:**
- Preserves: live metadata update/preservation, startup polling, publication, and Connect observation.
- Removes: callerless facades, discarded notifications/counters, fictional MP3 events, unused snapshot metric, and test-only shipped overloads.

- [ ] Move unique lyrics/preservation regressions onto `FillRecordingMetadata()` and `PreserveUpgradeMetadata()` before deleting the obsolete pipeline.
- [ ] Delete the reviewed dead paths only after reference tracing confirms no production caller remains.
- [ ] Update tests to call production APIs with explicit catalogs and remove obsolete build entries.
- [ ] Remove stale execution documents and replace them with concise capture/publication invariants and supported capability combinations.
- [ ] Run portable and Windows code-path suites and commit the green cleanup.

### Task 8: Reproducible automated gate and final verification

**Files:**
- Create: `.github/workflows/native-tests.yml`
- Modify: test runners only if required for clean noninteractive CI execution.

**Interfaces:**
- Produces: PR checks for the portable suite and deterministic sanitizer fuzzing.

- [ ] Add a GitHub Actions workflow running `test-native.sh` and `test-capture-fuzz.sh` on pull requests and pushes.
- [ ] Run the complete portable suite, Windows code-path suite, 50,000-mutation sanitizer campaign, production build, and privacy scan locally.
- [ ] Confirm PR #21 points to the final commit and `v1.3-RC1` is unchanged.
