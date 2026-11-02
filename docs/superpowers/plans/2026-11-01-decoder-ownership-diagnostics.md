# Decoder Ownership Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add evidence-only decoder ownership diagnostics that structurally prove codec-context relationships and measure decoder-session to Spotify playback-instance alignment without changing capture, listen, or publication decisions.

**Architecture:** Strengthen PE discovery so it returns checked codec-to-decoder context offsets, then feed compact decoded-output observations and existing PlayerAPI `playbackId` observations into a portable, fail-closed ownership tracker on the audio worker. Log anonymized state changes and analyze them offline; keep the current matcher authoritative until the live acceptance matrix passes.

**Tech Stack:** C++17, Windows x64 PE/unwind metadata, MinHook, SRW locks, existing PlayerAPI JavaScript collector, Bash/MinGW test scripts, Python 3 log analysis.

**Spec:** `docs/superpowers/specs/2026-11-01-decoder-ownership-diagnostics-design.md`

## Global Constraints

- Diagnostics only: do not change capture retention, listen eligibility, metadata assignment, publication, filenames, or acceleration behavior.
- Never select ownership using title, URI, duration, capture start time, EOS order, or temporal proximity.
- Never log raw pointers, raw `playbackId`, device IDs, decoded samples, compressed bytes, or unbounded payloads.
- No allocation, formatting, file I/O, metadata reads, or tracker mutation in hook callbacks.
- Any discovery inconsistency, pointer arithmetic failure, queue loss, clock regression, observation loss, or multiple viable successor must fail closed.
- Count decoded frames before acceleration drops PCM blocks; preserve the user's accepted accelerated-listen semantics.
- Keep AAC ownership unavailable in this phase; missing AAC evidence must not disable or alter existing AAC/FLAC/Ogg capture.
- Preserve `GetLastError`, callback quiescence, hook rollback, memory limits, and current worker-thread ownership.

## Review Focus

- **Ogg offset drift:** mutated outer/parser `lea` displacements must make discovery fail, covered by Task 1 synthetic mutation tests.
- **Context reuse and same-song replay:** reused addresses must receive new capture tokens and distinct `playbackId` values must receive new playback tokens, covered by Task 2 tracker tests.
- **Decoded observation pressure:** coalescing must not overflow counters silently; ring overflow must invalidate evidence without affecting acceleration, covered by Task 3 hook/queue tests.
- **Missing or stale PlayerAPI evidence:** empty/unchanged `playbackId`, timestamp regression, and media observation loss must never become `Bound`, covered by Task 2 and Task 5 tests.
- **Speculative/preloaded successors:** more than one successor or output before a playback-instance change must remain pending/conflicted, covered by Task 2 scenarios and Task 6 live acceptance analysis.

---

## File map

- Create `native/decoder_ownership.h`: portable ownership types, safe context normalization, tracker interface.
- Create `native/decoder_ownership.cpp`: bounded tracker state machine and anonymized token allocation.
- Create `tests/decoder_ownership_test.cpp`: portable state-machine and normalization tests.
- Create `tools/analyze_decoder_ownership.py`: offline diagnostic-log invariant checker.
- Create `tests/decoder_ownership_log_test.py`: deterministic analyzer fixtures.
- Modify `native/spotify_hook_discovery.h`: `DecoderOwnershipLayout` and strong discovery declaration.
- Modify `native/spotify_hook_discovery.cpp`: vtable/call-chain validation and extracted context offsets.
- Modify `native/audio_history.cpp`: small decoded-activity ring, hook observations, capture lifecycle integration, worker logging.
- Modify `tests/spotify_hook_discovery_test.cpp`: complete synthetic ownership layout and mutation cases.
- Modify `tests/windows_decoded_output_discovery_test.cpp`: validate and print the current-client ownership layout.
- Modify `tests/windows_playback_diagnostics_test.cpp`: real hook boundary, queue, capture lifecycle, privacy, and no-publication-change tests.
- Modify `test-native.sh`: build/run the portable tracker test.
- Modify `test-diagnostics.sh`: build/run analyzer tests and stronger current-client discovery.
- Modify `AUDIO-HISTORY.md`: document the temporary diagnostic lines and their privacy/fail-closed semantics.

### Task 1: Prove codec-to-decoder layout during discovery

**Files:**
- Modify: `native/spotify_hook_discovery.h`
- Modify: `native/spotify_hook_discovery.cpp`
- Modify: `tests/spotify_hook_discovery_test.cpp`
- Modify: `tests/windows_decoded_output_discovery_test.cpp`

**Interfaces:**
- Consumes: existing PE parsing, executable-section checks, x64 runtime-function lookup, `DiscoverHookTargets`, `DiscoverAacHookTargets`, and `DiscoverDecodedOutputTarget` anchors.
- Produces: `spotify::DecoderOwnershipLayout` and `spotify::DiscoverDecoderOwnershipLayout(const std::uint8_t*, std::size_t, DecoderOwnershipLayout&) noexcept` exactly as defined in the spec.

- [ ] **Step 1: Extend the synthetic PE fixture with the three codec vtables and connecting call chains**

Add fixture functions/instructions for the Ogg decoder-to-parser-to-page path, the FLAC vtable/client-data callback path, and the AAC compressed-reader vtable. Assert that valid discovery returns decoded RVA `0x4400`, Ogg delta `-0x88`, FLAC delta `0`, and `aac_verified == false` because no AAC reader-to-decoder relationship is proven.

- [ ] **Step 2: Add independent failure tests for every ownership link**

For each mutation, rebuild a fresh fixture, mutate only one instruction/displacement/vtable entry/direct call, and assert `InconsistentLayout`; add duplicate valid Ogg and FLAC chains and assert `AmbiguousTarget`. Include Ogg zero/out-of-range displacement encodings, FLAC client-data substitution, non-executable virtual slots, and a truncated PE range. Mutated AAC tables remain optional and must leave `aac_verified == false` without revoking FLAC/Ogg.

- [ ] **Step 3: Run the portable discovery test and confirm it fails**

Run:

```bash
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  tests/spotify_hook_discovery_test.cpp native/spotify_hook_discovery.cpp \
  -o build/spotify-hook-discovery-test
build/spotify-hook-discovery-test
```

Expected: compile failure because `DecoderOwnershipLayout` and `DiscoverDecoderOwnershipLayout` do not exist.

- [ ] **Step 4: Add the public layout type and strong discovery implementation**

Implement the exact signatures from the spec. Reuse `ParseImage`, `RuntimeFunction`, `ExecutableRange`, `FindTarget`, and checked `Read`/`Range`; add focused private helpers for unique vtable lookup, checked relative-call resolution, and bounded `lea` displacement extraction. Do not put current-client RVAs or `0x88` into the implementation.

- [ ] **Step 5: Make legacy decoded discovery share the stronger dispatcher/pump validator**

Keep `DiscoverDecodedOutputTarget` source-compatible. Factor the current dispatcher/pump checks so strong ownership discovery and acceleration discovery cannot drift, while allowing acceleration discovery to succeed independently when a codec ownership chain is unavailable.

- [ ] **Step 6: Update the mapped current-client test**

When a Spotify DLL argument is supplied, assert strong discovery succeeds and print the decoded RVA, three signed deltas, and AAC verification flag. No absolute addresses are printed.

- [ ] **Step 7: Run focused portable and current-client verification**

Run:

```bash
build/spotify-hook-discovery-test
bash test-diagnostics.sh "$SPOTIFY_DLL"
```

Expected: all tests pass; current client reports decoded RVA `0xd90d08`, Ogg delta `-136`, FLAC delta `0`, and AAC unverified.

- [ ] **Step 8: Commit the discovery proof**

```bash
git add native/spotify_hook_discovery.h native/spotify_hook_discovery.cpp \
  tests/spotify_hook_discovery_test.cpp tests/windows_decoded_output_discovery_test.cpp
git commit -m "test: prove decoder ownership hook relationships"
```

### Task 2: Implement the portable fail-closed ownership tracker

**Files:**
- Create: `native/decoder_ownership.h`
- Create: `native/decoder_ownership.cpp`
- Create: `tests/decoder_ownership_test.cpp`
- Modify: `test-native.sh`

**Interfaces:**
- Consumes: signed context deltas from Task 1, monotonic seconds, capture epoch, codec/stream identity, decoded activity aggregates, Spotify `playbackId`, URI, and existing listen generation.
- Produces: `NormalizeDecoderOwner`, `DecoderCodec`, `OwnershipState`, `CaptureAbortReason`, `ObservationLoss`, `DecodedActivity`, `OwnershipBinding`, and `DecoderOwnershipTracker` with the exact public signatures in the spec.

- [ ] **Step 1: Write normalization boundary tests**

Test zero context, positive and negative deltas, signed underflow, unsigned overflow, null result, misalignment, and the valid Ogg `capture - 0x88` case. Assert failure never modifies the output owner to a nonzero usable value.

- [ ] **Step 2: Write capture and playback state-machine tests**

Add named scenarios for one capture/one output/one playback instance, output before playback change, same URI with two distinct playback IDs, same URI with unchanged playback ID, empty playback ID, pause/resume, explicit skip, two pending successors, two live captures on one owner, context reuse after completion, output after abort, observation loss, epoch reset, timestamp equality/regression, and ten-second unresolved retention. Assert exact `OwnershipState` and tokens.

- [ ] **Step 3: Write bounded-storage and eviction tests**

Fill 33 captures, 17 owners, and 17 playback instances. Assert dependent records become `Invalidated`; eviction must not turn a `Conflict` or unresolved record into `Bound`.

- [ ] **Step 4: Run the new test and confirm it fails**

Run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  tests/decoder_ownership_test.cpp native/decoder_ownership.cpp \
  -o build/decoder-ownership-test
```

Expected: failure because the new files/interfaces do not exist.

- [ ] **Step 5: Implement safe normalization and the bounded tracker**

Use worker-owned fixed-capacity arrays or bounded vectors with explicit oldest-record invalidation. Tokens start at one and never expose source strings. Treat timestamp regression/equality where strict ordering is required as a conflict. Store playback IDs internally for equality only; provide no getter returning them.

- [ ] **Step 6: Add the focused test to the portable suite and run it**

Add the compile/run commands to `test-native.sh`, then run:

```bash
build/decoder-ownership-test
bash test-native.sh
```

Expected: tracker test and the full portable suite pass.

- [ ] **Step 7: Commit the ownership core**

```bash
git add native/decoder_ownership.h native/decoder_ownership.cpp \
  tests/decoder_ownership_test.cpp test-native.sh
git commit -m "feat: add fail-closed decoder ownership tracker"
```

### Task 3: Observe decoded activity without changing acceleration

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `tests/windows_playback_diagnostics_test.cpp`

**Interfaces:**
- Consumes: Task 1 `DecoderOwnershipLayout`, Task 2 `DecodedActivity` and `DecoderOwnershipTracker::ObserveDecoded`.
- Produces: private `DecodedActivitySlot`, `QueueDecodedActivity(...) noexcept`, and `DrainDecodedActivity(DecoderOwnershipTracker&, unsigned epoch)` in `audio_history.cpp`.

- [ ] **Step 1: Add hook-boundary tests at speed 1 and speed 10**

Extend the existing fake `Decode` path to assert that valid spans enqueue produced/consumed counts at both speeds, full produced floats are recorded before dropping, `kept_floats` matches `Accelerator::Keep`, and `GetLastError()==1234` after return. Assert the original dispatcher is called exactly once.

- [ ] **Step 2: Add invalid-span and disabled-diagnostics tests**

Assert invalid pointer advancement disables acceleration, increments ownership observation loss, enqueues no valid activity, and does not alter native return/error behavior. Diagnostics-disabled mode may suppress logs but must still collect evidence needed by the experiment.

- [ ] **Step 3: Add ring coalescing, saturation, overflow, and concurrency tests**

Exercise capacity 256, consecutive same-context coalescing, context/epoch boundaries, checked saturation, producer/consumer contention, and overflow. Assert overflow invalidates tracker evidence and does not touch the compressed capture queue's `dropped` counter.

- [ ] **Step 4: Run the focused Windows diagnostic test and confirm failures**

Run:

```bash
bash test-diagnostics.sh
```

Expected: compile/test failures for missing decoded-activity queue behavior.

- [ ] **Step 5: Add immutable ownership layout startup state**

Store the successful Task 1 layout before hooks are prepared. Clear it in `release_resources`. If strong ownership discovery fails, retain existing acceleration/capture behavior for this diagnostics phase and emit one diagnostic `ownership event=layout-unavailable` line.

- [ ] **Step 6: Implement the small fixed ring**

Use a dedicated `SRWLOCK`, 256 POD slots, and a dedicated atomic loss counter. Coalesce only a consecutive same-context/same-epoch tail. Use saturating additions and wake the existing worker event. No hook-side log or allocation.

- [ ] **Step 7: Refactor `DecodedSinkHook` in the prescribed order**

Always validate span advancement after the native call, compute full produced/consumed counts, apply current speed behavior unchanged, enqueue the compact observation, restore `GetLastError`, and return the native result. Do not call the tracker from the hook.

- [ ] **Step 8: Drain activity before media snapshots**

At the beginning of each worker iteration after capture progress/timing maintenance, drain decoded observations, normalize the already-decoded context with delta zero, and call `ObserveDecoded`. A changed loss counter calls `ObservationLost(ObservationLoss::DecodedQueue, now)` exactly once per change.

- [ ] **Step 9: Run diagnostics and acceleration regressions**

Run:

```bash
bash test-diagnostics.sh "$SPOTIFY_DLL"
bash test-native.sh
```

Expected: all tests pass; existing decoded-speed results are unchanged.

- [ ] **Step 10: Commit decoded observation**

```bash
git add native/audio_history.cpp tests/windows_playback_diagnostics_test.cpp
git commit -m "feat: observe decoded owner activity"
```

### Task 4: Attach ownership evidence to capture lifecycles

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `tests/windows_playback_diagnostics_test.cpp`

**Interfaces:**
- Consumes: Task 1 per-codec deltas; Task 2 `BeginCapture`, `CompleteCapture`, `AbortCapture`, and `Binding`.
- Produces: `Capture::decoder_owner`, `Capture::capture_token`, and complete lifecycle evidence for FLAC, Ogg serials, and AAC.

- [ ] **Step 1: Write FLAC, Ogg, and AAC lifecycle tests**

For FLAC and Ogg, assert begin/BOS creates a nonzero token with the expected normalized owner, completion preserves the token in `ready`, malformed input/error calls `AbortCapture`, and reinitialization/context reuse creates a new token. Assert AAC capture behavior remains unchanged and ownership-invalid. For two Ogg serials on one sync context, assert separate capture tokens share the normalized owner and become conflict evidence while simultaneously viable.

- [ ] **Step 2: Write normalization-failure and queue-overflow tests**

Use underflow/misaligned contexts and assert capture bytes may remain subject to existing behavior but ownership is `Invalidated`. Trigger compressed queue overflow and assert active ownership evidence and listen coverage are invalidated together; ready captures cannot later become uniquely bound.

- [ ] **Step 3: Run focused diagnostics to verify failures**

Run `bash test-diagnostics.sh`.

Expected: new lifecycle assertions fail because captures have no ownership fields or tracker calls.

- [ ] **Step 4: Add capture ownership fields and begin integration**

Add the two fields from the spec. At FLAC/AAC begin and Ogg BOS, normalize the raw hook context on the worker and call `BeginCapture(codec, owner, stream, time, epoch)`. Use Ogg serial as `stream`, zero for FLAC/AAC.

- [ ] **Step 5: Cover every completion and destruction path**

Call `CompleteCapture` immediately before moving a capture to `ready`. Centralize active-capture removal so every error, invalid page/frame, memory limit, reinitialize, timeout, settings reset, and queue overflow calls `AbortCapture` once with the matching bounded `CaptureAbortReason` before destruction. Do not call `AbortCapture` for a complete capture retained in `ready`.

- [ ] **Step 6: Add ownership to final audit diagnostics**

Have `CheckBeforeRelease` query the binding and include only numeric tokens/state in diagnostics. Keep the returned reason, byte validation, retention, and normal failure log behavior unchanged.

- [ ] **Step 7: Run focused and full native suites**

Run:

```bash
bash test-diagnostics.sh "$SPOTIFY_DLL"
bash test-native.sh
bash test-native-windows.sh
```

Expected: all suites pass with unchanged publication assertions.

- [ ] **Step 8: Commit capture lifecycle evidence**

```bash
git add native/audio_history.cpp tests/windows_playback_diagnostics_test.cpp
git commit -m "feat: track decoder ownership for capture sessions"
```

### Task 5: Feed playback instances and emit privacy-safe diagnostics

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `tests/windows_playback_diagnostics_test.cpp`
- Modify: `AUDIO-HISTORY.md`

**Interfaces:**
- Consumes: Task 2 playback/transition/loss methods, existing `Media.playback_instance`, `Media.spotify_uri`, validated `observed_at`, `Listen::generation`, and debug logger.
- Produces: state-change and bounded summary lines using only capture/owner/playback tokens.

- [ ] **Step 1: Add ordered media scenario tests**

Model Ready → Transition → Ready for different songs and identical-URI repeats. Assert a successor waits for a changed non-empty instance, then binds to the post-`AssociateListen` generation. Add empty instance, unchanged instance, manual skip, paused observation, stale/failed media, transition timestamp regression, and observation-overflow scenarios; assert none can incorrectly bind.

- [ ] **Step 2: Add privacy and rate-limit tests**

Use sentinel raw pointer, playback ID, device-like string, and URI values. Assert diagnostic lines contain numeric tokens/state but none of the sentinels. Generate continuous activity and assert at most one periodic output summary per owner per second, plus summaries on owner switch/completion/transition/reset.

- [ ] **Step 3: Add a no-behavior-change publication test**

Run the same capture/listen fixture twice with ownership diagnostics available and unavailable. Assert the current `SchedulePublications` result, save queue contents, filenames, and final validation run count are identical.

- [ ] **Step 4: Run focused diagnostics and verify failures**

Run `bash test-diagnostics.sh`.

Expected: media binding/log assertions fail because Worker does not feed the tracker.

- [ ] **Step 5: Feed Ready, Transition, and loss observations in Worker**

Call `ObservePlayback` after `AssociateListen` so the generation is current. Call `ObserveTransition` before `FinishAppTransition`. Debounce stale/failed/unavailable media into one `ObservationLost(ObservationLoss::Media, now)` per loss episode. Reset tracker with capture epoch changes before accepting new events.

- [ ] **Step 6: Implement diagnostic state-change formatting on the worker**

Log the exact bounded schemas in the spec only under `DebugLoggingEnabled()`. Allocate diagnostic tokens inside the tracker and expose numeric snapshots; never format source identities in the hook or tracker.

- [ ] **Step 7: Document the diagnostic option and interpretation**

In `AUDIO-HISTORY.md`, explain that `DebugLog=1` adds anonymized ownership evidence, diagnostics do not affect saving yet, `Bound` is experimental, missing/conflicting evidence fails closed for the future design, and raw addresses/playback IDs are not written.

- [ ] **Step 8: Run all code-path suites**

Run:

```bash
bash test-native.sh
bash test-diagnostics.sh "$SPOTIFY_DLL"
bash test-native-windows.sh
```

Expected: all suites pass; publication behavior remains byte-for-byte equivalent in focused assertions.

- [ ] **Step 9: Commit playback evidence and diagnostics**

```bash
git add native/audio_history.cpp tests/windows_playback_diagnostics_test.cpp AUDIO-HISTORY.md
git commit -m "feat: diagnose playback instance ownership"
```

### Task 6: Add an offline invariant analyzer and run the live acceptance gate

**Files:**
- Create: `tools/analyze_decoder_ownership.py`
- Create: `tests/decoder_ownership_log_test.py`
- Modify: `test-diagnostics.sh`

**Interfaces:**
- Consumes: only `ownership event=...` lines from `Floggfy-diagnostics.log`.
- Produces: exit `0` plus a scenario summary when all invariants hold; exit `1` with line-numbered violations for unknown tokens, invalid transitions, early preload binding, reused instance binding, binding after loss, multiple successors, or incomplete lifecycle.

- [ ] **Step 1: Write analyzer fixtures and expected failures**

Create logs for a valid distinct-song transition, valid same-URI/new-instance repeat, unresolved metadata timeout, preloaded capture, context reuse, multiple successors, queue loss, binding after loss, missing begin, duplicate completion, and raw-address/playback-ID leakage patterns. Assert valid logs pass and every invalid log fails with its named invariant.

- [ ] **Step 2: Run the analyzer test and confirm it fails**

Run `python3 tests/decoder_ownership_log_test.py`.

Expected: failure because the analyzer does not exist.

- [ ] **Step 3: Implement the streaming analyzer**

Use only the Python standard library. Parse key/value tokens with bounded line length, track capture/owner/instance state, reject duplicate or unknown identifiers, and print aggregate counts and violations. Do not attempt to reconstruct raw identities or guess missing events.

- [ ] **Step 4: Add analyzer tests to diagnostics suite**

Add `python3 tests/decoder_ownership_log_test.py` to `test-diagnostics.sh`, then run the full diagnostics suite with the current Spotify DLL.

- [ ] **Step 5: Build and install the diagnostic DLL**

Run:

```bash
bash build-native.sh
```

Verify `build/version.dll` exists and passes the repository's existing build checks. Install it using the already selected client installation mode, preserving a recoverable copy of the previously installed DLL, then fully restart Spotify. Do not mix launcher and proxy installation modes.

- [ ] **Step 6: Enable bounded diagnostics for the live run**

In the installed `SpotifyHistory.ini`, set `Log=1` and `DebugLog=1`. Leave networking and raw hook profiling disabled unless a separate performance diagnosis requires them. Record the DLL hash, Spotify DLL hash/version, capture speed, codec setting, crossfade setting, and scenario start/end markers outside the application log.

- [ ] **Step 7: Execute the acceptance matrix**

Run every scenario in the spec at its listed speed/codec. Include at least two repetitions of same-song replay and distinct-song transition at 10x. Preserve `Floggfy-diagnostics.log` before its 5 MiB cap rolls over.

- [ ] **Step 8: Analyze the live log and enforce the gate**

Run:

```bash
python3 tools/analyze_decoder_ownership.py /path/to/Floggfy-diagnostics.log
```

Expected for success: exit `0`, no privacy violation, no binding after loss, no early preload binding, distinct playback tokens for same-song repeats, and one owner per publishable capture. Any failure stops production migration and opens a new playback-pump ABI investigation; do not weaken the invariant.

- [ ] **Step 9: Restore ordinary diagnostic settings**

Return temporary debug settings to their prior values after evidence is collected. Keep or restore the tested DLL according to the release workflow; record which version remains installed.

- [ ] **Step 10: Commit the analyzer**

```bash
git add tools/analyze_decoder_ownership.py tests/decoder_ownership_log_test.py test-diagnostics.sh
git commit -m "test: gate decoder ownership with runtime evidence"
```

## Completion gate

This plan is complete only when Tasks 1–6 pass and the live analyzer reports no invariant violation. That result authorizes writing a separate production-migration plan. It does not itself authorize removing the current matcher or making diagnostic `Bound` records control publication.
