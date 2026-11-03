# General Log Levels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `Log`/`DebugLog` with one cumulative `LogLevel`, use local ISO timestamps, and isolate debug, networking, and hook diagnostics in dedicated files.

**Architecture:** Settings expose one exact numeric level and independent diagnostic opt-ins. Producers explicitly classify successful track completion separately from other activity; the asynchronous logger filters centrally and routes each diagnostic source to a bounded queue and fixed file.

**Tech Stack:** C++17, Win32 profile/time/file APIs, existing asynchronous logger and Windows-native tests.

**Spec:** `docs/superpowers/specs/2026-11-03-log-level-design.md`

## Global Constraints

- Accept exactly `LogLevel=0`, `1`, `2`, or `4`; missing/invalid values normalize to `1`.
- Level 1 records completed-track outcomes, warnings, and failures; level 2 adds user-visible lifecycle activity; level 4 adds diagnostics.
- `DebugNetworking` and `DebugHookDiagnostics` are independent opt-ins effective only at level 4.
- Never route records based on message text or add raw payloads, credentials, URLs, media, or device identifiers.
- Preserve queue bounds, file caps, nonblocking producers, and logging-failure containment.
- Verify only changed settings, logger, diagnostics, and the directly changed publication call path.

## Review Focus

- An INI without `LogLevel` must ignore obsolete `Log`/`DebugLog` values and insert `LogLevel=1`; Task 1 tests this.
- Duplicate, padded, or unsupported `LogLevel` input must normalize to one authoritative `LogLevel=1`; Task 1 tests this.
- Level 1 must retain failures and warnings while rejecting starts, upgrades, renames, and other level-2 activity; Task 2 tests the full matrix.
- Local timestamps must remain unambiguous across non-UTC zones by including a signed offset; Task 2 tests the formatter against Win32 conversion.
- Saturation or opt-out in one diagnostic lane must not consume capacity or suppress records in another lane; Task 3 tests independent queues and omission reports.

---

### Task 1: Replace legacy general logging settings

**Files:**
- Modify: `native/history_settings.h`
- Modify: `native/history_settings.cpp`
- Modify: `native/settings_file.h`
- Modify: `native/collector_config.h`
- Modify: `SpotifyHistory.ini`
- Modify: `test-diagnostics.sh`
- Test: `tests/windows_settings_test.cpp`
- Test: `tests/windows_pr13_settings_test.cpp`

**Interfaces:**
- Produces: `Settings::log_level` as `unsigned`, default `1`.
- Produces: `unsigned LogLevel()` and level-derived `LoggingEnabled()` / `DebugLoggingEnabled()`.
- Produces: `settings_file::Erase(std::wstring&, const wchar_t*)` for removing all occurrences of obsolete keys in `[To Disk]`.
- Removes: `Settings::log`, `Settings::debug_log`, `log_enabled`, `debug_enabled`, and `DiagnosticsEnabled(bool,bool)`.

- [ ] **Step 1: Add failing settings and migration tests**

Extend `windows_settings_test.cpp` with `log-0`, `log-1`, `log-2`, `log-4`, `log-invalid`, `log-padded`, `legacy-log-zero`, and `legacy-debug-one`. Assert the exact in-memory level, normalized persisted `LogLevel`, obsolete-value ignorance, and absence of `Log`/`DebugLog` after normalization. Extend `windows_pr13_settings_test.cpp` to assert level-4 collector debug gating and independent networking/hook settings.

- [ ] **Step 2: Add the log settings scenarios to the focused runner**

Teach `test-diagnostics.sh` to compile `windows_settings_test.cpp` in its own temporary directory and run only the eight `log-*` / `legacy-*` scenarios from Step 1. Keep its existing `windows_pr13_settings_test.cpp` execution.

- [ ] **Step 3: Run the focused settings tests and verify failure**

Run: `bash test-diagnostics.sh`

Expected: FAIL because `Settings::log_level`, `LogLevel()`, and legacy-key removal do not exist.

- [ ] **Step 4: Implement strict parsing and one-time migration**

Add `ReadLogLevel(Settings&, const std::wstring& path)` in `history_settings.cpp`. Exact new values win. Missing, invalid, or duplicate input becomes `1` without consulting obsolete keys; persist `LogLevel`, erase both legacy keys, and make collector options use `settings.log_level>=4`.

- [ ] **Step 5: Run the focused settings tests**

Run: `bash test-diagnostics.sh`

Expected: every new settings scenario and the PR13 settings test passes.

- [ ] **Step 6: Commit**

```bash
git add native/history_settings.h native/history_settings.cpp native/settings_file.h native/collector_config.h SpotifyHistory.ini tests/windows_settings_test.cpp tests/windows_pr13_settings_test.cpp test-diagnostics.sh
git commit -m "feat: replace logging switches with log level"
```

### Task 2: Enforce event levels and local timestamps centrally

**Files:**
- Modify: `native/async_log.h`
- Modify: `native/async_log.cpp`
- Test: `tests/windows_networking_log_test.cpp`
- Test: `tests/windows_renderer_privacy_test.cpp`

**Interfaces:**
- Consumes: `unsigned LogLevel()` from Task 1.
- Produces: `LogKind::Finished`, whose minimum level is 1.
- Preserves: `LogKind::Activity` at level 2, `Warning`/`Failure` at level 1, and `Diagnostic` at level 4.
- Produces: local ISO 8601 record formatting with `±HH:MM`; diagnostics retain milliseconds.

- [ ] **Step 1: Add a failing logger-level matrix**

For levels `0`, `1`, `2`, and `4`, assert queue admission for `Finished`, `Activity`, `Warning`, `Failure`, and `Diagnostic`. Include event text containing misleading words to prove classification never parses messages.

- [ ] **Step 2: Add a failing deterministic local-time formatting test**

Construct a `Record` with a fixed UTC `SYSTEMTIME`, calculate the expected local time through Win32, and assert the formatted record has the matching wall time, a signed numeric offset, no trailing `Z`, and milliseconds only for diagnostic output.

- [ ] **Step 3: Run the focused logger tests and verify failure**

Run: `bash test-diagnostics.sh`

Expected: FAIL because all normal activity currently shares one class and formatting is UTC.

- [ ] **Step 4: Implement minimum-level filtering and local formatting**

Add `Finished` to `LogKind`; map kinds to numeric thresholds inside `LogEvent`. Keep renderer-reserved input handling ahead of ordinary routing. Convert each stored UTC observation to local time during formatting and append its event-time numeric UTC offset.

- [ ] **Step 5: Run the two focused logger tests**

Run: `bash test-diagnostics.sh`

Expected: both pass with the level matrix and local timestamps.

- [ ] **Step 6: Commit**

```bash
git add native/async_log.h native/async_log.cpp tests/windows_networking_log_test.cpp tests/windows_renderer_privacy_test.cpp
git commit -m "feat: filter logs by level and use local time"
```

### Task 3: Isolate diagnostic output lanes

**Files:**
- Modify: `native/async_log.h`
- Modify: `native/async_log.cpp`
- Modify: `native/hook_diagnostics.h`
- Test: `tests/windows_networking_log_test.cpp`
- Test: `tests/windows_renderer_privacy_test.cpp`
- Test: `tests/windows_deep_diagnostics_test.cpp`

**Interfaces:**
- Produces: `void QueueHookDiagnostic(const char*) noexcept`.
- Routes: ordinary debug to `Floggfy-diagnostics.log`, networking to `Floggfy-networking.log`, and hook observations to `Floggfy-hook-diagnostics.log`.
- Preserves: `QueueNetworkingDiagnostic(const char*) noexcept` for sanitized networking only.

- [ ] **Step 1: Add failing independent-routing tests**

Assert each producer writes only its dedicated queue/file, each queue has its own 128-record capacity and omission summary, networking remains gated by `DebugNetworking`, hook diagnostics remain gated by `DebugHookDiagnostics`, and both require level 4 but not each other.

- [ ] **Step 2: Run the three focused diagnostics tests and verify failure**

Run: `bash test-diagnostics.sh`

Expected: FAIL because networking and hook records currently share the diagnostic queue/file and hook gating depends on networking.

- [ ] **Step 3: Implement four explicit lanes**

Replace the record-level networking flag with an internal lane enum and one bounded queue per file. Make the logger worker drain a bounded batch from every lane fairly. Route sanitized renderer networking to the networking lane, Connect/general debug to the diagnostic lane, and `PollHookDiagnostics()` output through `QueueHookDiagnostic`.

- [ ] **Step 4: Run the three focused diagnostics tests**

Run: `bash test-diagnostics.sh`

Expected: all pass, including privacy rejection and independent saturation.

- [ ] **Step 5: Commit**

```bash
git add native/async_log.h native/async_log.cpp native/hook_diagnostics.h tests/windows_networking_log_test.cpp tests/windows_renderer_privacy_test.cpp tests/windows_deep_diagnostics_test.cpp
git commit -m "feat: isolate diagnostic log files"
```

### Task 4: Classify production completion and run the changed-path gate

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `docs/capture-invariants.md`
- Test: `tests/windows_playback_diagnostics_test.cpp`
- Test runner: `test-diagnostics.sh`

**Interfaces:**
- Consumes: `LogKind::Finished` from Task 2.
- Produces: successful new recording publication as the sole level-1 success event; upgrades remain level-2 activity.
- Produces: `PublicationLogKind(Publication,bool newly_saved)` as a pure internal classification helper used by publication and its regression.

- [ ] **Step 1: Add a failing publication classification regression**

Extend `windows_playback_diagnostics_test.cpp` to exercise `PublicationLogKind`: a newly saved successful recording returns `LogKind::Finished`; upgraded, skipped, and other activity returns `LogKind::Activity`; failed publication returns `LogKind::Failure`. Existing logger tests preserve readable event names and identity details.

- [ ] **Step 2: Run the focused playback diagnostics executable and verify failure**

Run: `bash test-diagnostics.sh`

Expected: FAIL because both outcomes currently use `LogKind::Activity`.

- [ ] **Step 3: Classify successful new publication explicitly**

Add the pure internal `PublicationLogKind(Publication,bool)` helper and use it from the publication completion path. Pass `newly_saved=true` only after `FilePublication::Saved`; an `Upgraded` result remains activity. Keep starts, renames, metadata updates, skips, settings changes, and auto-skip requests as `Activity`.

- [ ] **Step 4: Update maintained logging documentation**

Document the four exact levels, legacy migration, local timestamp format, four file destinations, independent networking/hook opt-ins, and privacy constraints in `docs/capture-invariants.md`.

- [ ] **Step 5: Run the changed-path verification gate**

Run:

```bash
bash test-diagnostics.sh
bash build-native.sh
git diff --check
```

Expected: all diagnostic/settings/logger/publication regressions pass, the production DLL builds, and the diff has no whitespace errors. Do not run unrelated portable capture or client playback suites.

- [ ] **Step 6: Commit**

```bash
git add native/audio_history.cpp docs/capture-invariants.md tests/windows_playback_diagnostics_test.cpp
git commit -m "feat: classify finished track logging"
```
