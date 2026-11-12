# Stream-owned recording implementation plan

> Execute inline using superpowers:executing-plans. Review the whole change after verification.

**Goal:** Publish complete identified streams independently of playback coverage;
save complete validated unmatched audio after MaxMatchQueue subsequent media starts and
discard incomplete or corrupt preloads.

**Architecture:** Three independent stages: stream buffers/codec completion,
metadata/history collection, and one dedicated matcher/finalizer. Stable stream
and playback generations retain basic start/end/length/completeness evidence.
Only the matcher/finalizer joins records and schedules identified or aged
unmatched publication. Successful saves release RAM; retries retain it.

**Tech stack:** Existing C++17 Windows hooks/readers, MinGW cross-build, native and
Windows regression executables, local real-dump replay.

**Spec:** docs/superpowers/specs/2026-11-12-stream-owned-recording-design.md

## Constraints

- Default MaxBufferedMiB=512; SaveUnmatched=1; Unmatched lives under save root.
- MaxMatchQueue defaults to five distinct subsequent media starts, including
  repeats; pauses do not count. Accepted range: 1–100.
- Update/merge PR #25 and publish the next 1.3 release candidate only after the
  complete-only unmatched fix and required checks pass. Audit the RC version;
  final 1.3 is outside this authorization. Preserve debug INI and dumps.
- Unmatched saves require intrinsic codec completeness; no incomplete .bin
  archival. Raw DumpStreams remains a separate opt-in debug tool.
- Codec completeness and exact identity override UI chronology. No fabricated
  completeness from elapsed time, approximate MP3 duration, or cleanup.
- `[Matcher] TimeOffsetToleranceMs=10000` accepts 0–120000 ms.
  `DurationToleranceMs=250` accepts 0–2000 ms and allows approximately 200 ms of
  metadata duration error. Candidate timing/length filters do not relax codec
  completeness. Exact GUI IDs may join outside the time window, with native
  ownership observations valid for that capture lifecycle.
- Log start/end/duration deltas for debug decisions. End deltas are diagnostic
  only; there is no separate end-time gate.

## Review focus

Prefetch and decoder reuse; late/paused metadata; truncated MP3 aligned to frames;
repeated song counting; disk-write failure and ownership of buffered bytes.

## Tasks

1. [x] Finish regressions for real Ogg recording publication without a
   completed listen, five-song unmatched retirement, pauses/repeats, and successful
   publication releasing its reservation. Review regressions have reproduced
   issues; corrections and focused Windows validation now pass.
2. [x] Finish replacing completed-listen coordination in native/audio_history.cpp
   with separate stream/metadata collection and a sole matcher/finalizer. Retire obsolete
   transition-completion branches and adapt tests that asserted the old contract.
   Preserve strong ownership, ambiguity evidence, formats and publication rules.
3. [x] Implement MP3 declared frame/sample proof in native/mp3_stream.* with
   native parser regressions. Source-boundary closure remains independent of UI
   pause/end observations; keep incomplete final items buffered.
4. [x] Add default-on SaveUnmatched and bounded matcher INI settings, preserving
   external edits and adding missing defaults. Verify parser, section isolation,
   Unicode persistence/reload, and public/embedded default parity.
5. [x] Finish validating unmatched archival with unique timestamp filenames and
   correlation sidecars, complete-only codec guards, and incomplete/corrupt
   preload disposal after MaxMatchQueue subsequent starts. Retain complete writes for
   retry and release bytes after successful saves or disabled unmatched saves.
6. [ ] Complete current review corrections and latest focused Windows tests,
   rerun affected replay/native checks, build and review the candidate, then
   install/restart for live playlist validation as applicable. Update/merge
   PR #25 and publish the audited next 1.3 RC after required checks pass.

## Execution ledger

- Before implementation: real Ogg session replay reproduces the Queen rejection;
  stream has 23 contiguous CRC-valid pages and 177858 samples at 44100 Hz.
- Work in the existing PR checkout: preserve the user's ongoing branch and local
  replay changes; a second worktree would split the active testing workspace.
- Updated ruling: only codec-validated complete supported unmatched sources
  save with their audio extension and a sidecar. Incomplete/corrupt preloads
  are discarded at the configured start limit; raw DumpStreams is separate.
- Portable native implementation/checks and settings work are done. Earlier
  full native/Windows suites passed before the latest review corrections;
  those runs are not final evidence for the current full change.
- Verified settings checks: expanded Windows PR13 settings test covers custom,
  zero/max, missing/invalid/duplicate values, section isolation, default insertion,
  external edits, and restart-only behavior; 15 installation/log/fallback
  scenarios passed. Unicode legacy migration, locked-write failure, concurrent
  persistence, and fresh-process custom-value reload passed.
- `python3 tests/diagnostics_source_test.py` passed, including exact public and
  embedded INI parity; `git diff --check` passed after the settings changes.
- Final matcher settings checks passed with MaxMatchQueue, TimeOffsetToleranceMs
  and DurationToleranceMs: defaults/custom/max/zero/invalid/duplicate values,
  legacy 0/3/10/120-second migration, new-key precedence, section isolation,
  actual persistence, external edits, and fresh-process Unicode reload. The
  same 15 installation/log/fallback scenarios and default parity passed again.
- Latest focused Windows review regressions and complete-only unmatched matrix
  pass, including inherited native IDs, cold layout learning, blocked save
  fairness, and queue-ownership counters. Final full Windows gate, merged-tree
  build, installation, and live playlist validation remain pending.
- Buffer/count instrumentation is implemented with move-only ownership leases,
  queue-full rollback, and debug-only changed summaries. Portable optimized and
  sanitizer checks pass; the latest Windows primary suite passes.
- User subsequently authorized updating/merging PR #25 and publishing a new
  1.3 RC after the complete-only unmatched correction; audit selected v1.3-RC7.
