# Native-Format Podcasts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Save fully verified Spotify episodes in their original MP3, Ogg, AAC, or FLAC format without using codec as a proxy for media type.

**Architecture:** Decoder capture and validation remain codec-specific and media-neutral. Capture/listen reconciliation uses ownership, playback instance, timing, and duration; only the publication stage classifies metadata and applies independent source-format and podcast policy.

**Tech Stack:** C++17, Windows x64 hooks, MinGW-w64 test executables, shell test wrappers, existing Ogg/FLAC/AAC/MP3 validators.

**Spec:** `docs/superpowers/specs/2026-11-12-native-format-podcasts-design.md`

## Global Constraints

- Preserve original compressed bytes; never transcode.
- Preserve exact ownership, complete-listen, duration, ambiguity, and final-container validation.
- `Podcasts=1` is required for episodes; Ogg, FLAC, and AAC episodes also require their source-format switch.
- MP3 has no separate format switch and remains allowed when the media policy permits publication.
- Do not publish historical dump files automatically.
- Do not mix unrelated worker cleanup into the behavioral fix.

## Review Focus

- A complete Ogg episode with exact ownership must bind and publish under `Podcasts`, not be rejected as non-MP3.
- A complete FLAC or AAC episode must obey both `Podcasts` and its codec switch.
- MP3 episode behavior must remain unchanged, including decoder reuse and terminal-context completion.
- Track association must remain codec-neutral without allowing ambiguous or mismatched-duration captures.
- A terminal episode with a complete bound non-MP3 capture must use the same guarded end inference as MP3.

---

### Task 1: Separate Source-Format and Media Publication Policy

**Files:**
- Create: `native/download_policy.h`
- Create: `tests/download_policy_test.cpp`
- Modify: `test-native.sh`
- Modify: `native/audio_history.cpp:1056-1058,1265-1400`

**Interfaces:**
- Produces: `bool SourceFormatEnabled(Codec, const Settings&) noexcept`
- Produces: `bool MediaTypeEnabled(MediaKind, const Settings&) noexcept`
- Produces: `bool PublicationEnabled(Codec, MediaKind, const Settings&) noexcept`

- [ ] **Step 1: Write the failing policy matrix test**

Add `tests/download_policy_test.cpp` with assertions that Ogg/FLAC/AAC follow their codec switches, MP3 is source-enabled without consulting `Podcasts`, all episodes require `Podcasts`, and ordinary music is not rejected by the podcast switch. Add its compile/run command to `test-native.sh`.

- [ ] **Step 2: Run the focused policy test and verify RED**

Run the new compile/run fragment from `test-native.sh`.
Expected: compilation fails because `native/download_policy.h` and its interfaces do not exist.

- [ ] **Step 3: Implement the pure policy and use it at both publication setting checks**

Implement the three `noexcept` inline functions in `native/download_policy.h`. In `Publish`, calculate `CatalogFor(heard.media)` before the first policy check and replace both codec-only checks with `PublicationEnabled(c.Encoding().codec,catalog.kind,settings)`; retain generation and master-download checks inside the centralized policy.

- [ ] **Step 4: Run the policy test and focused publication/association tests**

Run: `bash test-native.sh`
Expected: the new policy matrix and existing portable suite pass.

- [ ] **Step 5: Commit the policy separation**

Commit the policy header, test, runner change, and publication call sites as one independently reviewable change.

### Task 2: Make Capture/Playback Association Media-Neutral

**Files:**
- Modify: `tests/windows_quality_observer_test.cpp`
- Modify: `native/audio_history.cpp:1850-1950`

**Interfaces:**
- Consumes: existing `BindReadyCapture(...)` and `BindDirectReadyCapture(...)`
- Produces: association candidates selected without `capture.mp3 == episode` or `capture.mp3 != episode` predicates

- [ ] **Step 1: Write failing real-association regressions**

Extend `TestLateAssociation()` with complete, validated Ogg and FLAC captures whose ownership tracker has observed decoded output. Assert that each binds to exact `spotify:episode:` playback metadata, retains the listen generation, and schedules only the uniquely matching capture. Add negative assertions for mismatched duration and two viable candidates.

- [ ] **Step 2: Run the focused association test and verify RED**

Run: `bash test-capture-association.sh`
Expected: the Ogg/FLAC episode binding assertions fail because current association filters episode candidates by `capture.mp3`.

- [ ] **Step 3: Remove media/codec equivalence from both binding paths**

In `BindReadyCapture` and `BindDirectReadyCapture`, retain URI support, ownership state, direct decoder match, duration, candidate uniqueness, and ambiguity checks, but remove codec comparisons against episode/track classification. Keep MP3-specific stream-boundary staging and interstitial exclusion unchanged.

- [ ] **Step 4: Run focused association and playback diagnostics**

Run: `bash test-capture-association.sh`
Run: `bash test-diagnostics.sh`
Expected: all association, terminal, decoder-reuse, pause/gap, and diagnostic tests pass.

- [ ] **Step 5: Commit media-neutral association**

Commit only the association regressions and minimal predicate removal.

### Task 3: Generalize Guarded Terminal Completion

**Files:**
- Modify: `tests/windows_playback_diagnostics_test.cpp`
- Modify: `native/audio_history.cpp:2570-2620`

**Interfaces:**
- Consumes: `BoundCompleteCaptureForListen(...)` and `FinalCaptureValidation`
- Produces: terminal-context completion for any uniquely bound, duration-matching, fully validated codec

- [ ] **Step 1: Write a failing non-MP3 terminal regression**

Add an Ogg episode variant of the explicit terminal-context scenario. Assert that a fresh monitored terminal clear completes only when the capture is bound, complete, final-valid, duration-matching, and the projected playback clock reaches the end; assert rejection for incomplete, stale, controlled, or ambiguous variants.

- [ ] **Step 2: Run the focused terminal test and verify RED**

Run the playback executable through `bash test-diagnostics.sh` (or its focused `--podcast-end` mode during iteration).
Expected: the valid Ogg terminal case remains incomplete because the terminal branch currently requires `capture->mp3`.

- [ ] **Step 3: Generalize the terminal proof**

Allow the existing terminal-context inference after `BoundCompleteCaptureForListen` returns any validated capture. Do not change the episode-only terminal assertion contract, freshness window, projected-end check, controls, ownership hold, or ambiguity rejection.

- [ ] **Step 4: Run diagnostics and the real dump integrity audit**

Run: `bash test-diagnostics.sh`
Run the local Ogg dump audit against streams 9-11 and require continuous sequence numbers, zero CRC failures, EOS, and decoded/source durations equal to metadata within one millisecond.
Expected: all synthetic tests pass; all three local captures satisfy the stated integrity conditions.

- [ ] **Step 5: Commit terminal generalization**

Commit the Ogg terminal regression and minimal guarded condition change.

### Task 4: Full Verification and Trimming Review

**Files:**
- Modify: `VALIDATION.md`
- Review: `native/audio_history.cpp`
- Review: `native/history_settings.cpp`
- Review: affected tests and shell wrappers

**Interfaces:**
- Produces: verified release evidence and a separate list of safe trimming opportunities

- [ ] **Step 1: Run full verification**

Run: `bash test-native.sh`
Run: `bash test-native-windows.sh`
Run: `bash build-native.sh`
Run: `git diff --check`
Expected: every command exits 0 with no new warning/error output.

- [ ] **Step 2: Review the fixed pipeline for unnecessary work**

Inspect capture hooks, binding, terminal handling, policy checks, worker polling, logging, and dump-only paths. Identify duplicated classification, redundant state, unnecessary background work, and tests that encode obsolete assumptions. Do not remove safety checks merely because they are codec- or transition-specific.

- [ ] **Step 3: Apply only mechanically safe trims covered by existing tests**

If review finds dead predicates, duplicate policy checks, stale comments, or unreachable branches directly superseded by the new policy, remove them in a separate commit and rerun focused plus full tests. Report larger refactors as follow-up work rather than expanding this fix.

- [ ] **Step 4: Record validation evidence**

Update `VALIDATION.md` with the root cause, red/green regressions, dump integrity results, full-suite results, build hash, and code-review findings.

- [ ] **Step 5: Request final code review**

Review the complete branch diff for correctness, safety regressions, unnecessary complexity, and scope. Resolve actionable findings, rerun affected tests, and keep the release blocked until a fresh live native-format podcast publishes.
