# Capture/save correctness and recovery implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent wrong associations and lost completed recordings, explain failed matches, and add bounded recoverable publication.

**Architecture:** Preserve independent codec capture, occurrence history and matching. Separate immutable audio ownership, prepared artifacts and destination transactions; introduce durable job ownership only after the immediate correctness changes pass. Each numbered task is an independently reviewed commit, with the recovery stage gated on the proposed policies in the design.

**Tech Stack:** C++17, existing Windows hooks and file transactions, MinGW/WSL, portable tests, Windows production-path tests and real stream-dump replay.

**Spec:** [Capture/save correctness and recovery design](../specs/2026-11-13-capture-save-recovery-design.md), derived from [PR #29](https://github.com/Mainkill1/Floggfy/pull/29).

## Global constraints

- Preserve `MaxBufferedMiB=512`, `SaveUnmatched=1` and complete-only unmatched archival.
- Preserve `[Matcher] MaxMatchQueue=5`, `TimeOffsetToleranceMs=10000`, `DurationToleranceMs=250`, current ranges and millisecond units.
- Retirement counts subsequent observed playback starts; no wall-clock expiry or timing-only identity proof.
- Preserve codec validation, original payloads, failed-enqueue restoration and existing safe file-publication transactions.
- No decoder-callback I/O; all diagnostics and recovery collections must be bounded.
- No new DLL installation, merge or release during planning. Implementation requires regression, Windows and live-playback gates before release.

## Review focus

1. Late native identity arriving after fallback occurrence creation must enrich the same occurrence without double counting (Task 1).
2. Evidence pruning/overflow and invalid preloads must not turn ambiguity into a match (Task 2).
3. Root changes during staged publication and commit-before-acknowledgement must preserve the committed output (Tasks 4, 8, 10).
4. A failed rebuild or denied peak reservation must retain the only usable audio representation (Tasks 5, 7).
5. Logger saturation and malformed/restarted durable state must report lost coverage or block safely without changing matching (Tasks 9, 10).

## Evidence and priorities

All findings below were checked against main `4b559ad`; no new executable tests were run for this planning review. Earlier RC7 test passes do not cover these scenarios.

| Priority | Source-confirmed conditional problem | Owner/task |
| --- | --- | --- |
| P1 | Retiring a competing capture can manufacture weak uniqueness | 2 |
| P1 | Settings generation drift returns terminal Skipped and destroys completed audio | 4 |
| P1 | Retry pairs fresh catalog/enrichment with stale prepared bytes | 5 |
| P2 | A → B → A without playback instances reuses an old occurrence | 1 |
| P2 | Label lookup loses URI-specific enrichment when text is ambiguous | 3 |
| P2 | Front-only retry deque hides an earlier deadline | 6 |
| P2 | AAC/flatten construction peaks omit simultaneously retained source memory | 7 |
| P2 | Pending unmatched retries retain paths under the old root | 8 |
| Design | Persistent failures pin shared capture RAM, with insufficient recovery visibility | 10–11 |
| Diagnostics | Failed-match inventory and evidence coverage are incomplete | 9 |

## File and ownership map

- Existing `native/recording_metadata.h`: occurrence creation, enrichment, start counting and pruning.
- Existing `native/audio_history.cpp`: production matcher, capture handoff and worker integration; remove replaced state only after its behavior moves.
- Existing `native/metadata_bridge.*`, `native/album_artwork.*`: exact-URI merges and coherent RAM-only preparation inputs.
- Proposed `native/publication_job.h`: move-only source ownership, outcomes, artifact and destination-plan types. Keep transaction primitives in existing `native/file_publication.*`.
- Proposed `native/retry_schedule.h`: portable deadline ordering, independent of Windows sleeping/thread APIs.
- Existing `native/compressed_buffer.*`, `native/aac_stream.h`, `native/aac_container.cpp`, `native/recording_limits.h`: construction planning and pre-allocation reservations.
- Proposed `native/matcher_diagnostics.*`: bounded snapshot storage/serialization; production decisions originate in `audio_history.cpp`.
- Proposed `native/pending_recordings.*`: durable completed-job store, manifests, quota, recovery and status. This is separate from matching and forensic DumpStreams.
- Existing `native/history_settings.*` and To Disk menu implementation: only scoped revision changes, approved recovery settings/status/actions; find the actual menu owner before editing.
- Extend existing production-path tests and their shell runners; new portable modules receive focused test executables wired into `test-native.sh`.

## Stage A: correctness and evidence

### Task 1: distinguish playback occurrences from recording identity

**Files:** `native/recording_metadata.h`, `native/audio_history.cpp`, new `tests/recording_metadata_test.cpp`, `tests/windows_playback_diagnostics_test.cpp`, `test-native.sh`.

**Interfaces:** Preserve `RecordingHistory::Observe(const Media&, double)` and its generation-based consumers. Native playback instance remains authoritative; fallback lookup targets only the continuous current occurrence. A confirmed departure/return creates a fresh generation. Explicit restart evidence must come from the existing provider/lifecycle, never an arbitrary backseek.

- [ ] Add `FallbackAlternationCountsEveryStart`: 23 alternating observed starts without IDs yield `Starts()==23`; returned A is unassigned with a fresh generation. Add pause/resume, duplicate snapshot, stale-event and late native-ID attachment cases; explicit native IDs retain existing behavior.
- [ ] Run `bash test-native.sh`; confirm the new fallback regression fails for occurrence reuse before changing production code.
- [ ] Implement occurrence transitions and preserve delayed sparse metadata merging. Reject older observations from changing the current occurrence or its start count.
- [ ] Run the portable suite and `bash test-native-windows.sh`; verify retirement at exactly the configured subsequent-start threshold and no pause expiry. Commit the occurrence fix.

### Task 2: preserve ambiguity when matching evidence retires

**Files:** `native/audio_history.cpp` (`AssociateRetainedRecordings`, `ScheduleStreamPublications`, loss/discard paths), `native/recording_metadata.h` (prune seam), `tests/windows_playback_diagnostics_test.cpp`.

**Interfaces:** Add a bounded `RetiredMatchEvidence` owner on the capture worker, with `RememberCapture`, `RememberMetadata`, `BlocksWeakMatch` and `Prune` operations. Store stable IDs and compatibility facts, not audio or owning metadata leases. Overflow creates a conservative lost-evidence fence; cleanup never clears uncertainty solely because a record vanished. Exact native binding bypasses only uncertainty that its evidence genuinely resolves.

- [ ] Add `RetiredCompetitorDoesNotCreateMatch`: S1 at start 1 and S2/B at start 2 remain ambiguous after S1 retires at start 6 with queue 5. Cover successful unmatched archival, SaveUnmatched=0, incomplete S1, active/ready competitors, retired metadata and bounded-evidence overflow. Add a later exact-native resolution case.
- [ ] Run `bash test-native-windows.sh`; confirm the retirement case reproduces the wrong association.
- [ ] Record evidence before every relevant removal and use it in the actual weak-match uniqueness decision. Retain the existing timing/duration/native-ownership rules; do not permanently reject a later proven match.
- [ ] Rerun Windows production matcher tests and `bash test-capture-association.sh`. Verify bounded evidence memory and unchanged ownership counters. Commit separately.

### Task 3: merge enrichment by exact URI

**Files:** `native/metadata_bridge.cpp` (`MetadataWorker`, `EnrichmentWorker`), `tests/windows_metadata_liveness_test.cpp`.

**Interfaces:** Use existing `MetadataCache::FindUri` when a URI is known; use label lookup only without an exact URI. Preserve locks, freshness checks, sparse merge precedence and lyrics policy.

- [ ] Add two different URIs with identical labels/duration. Sparse refresh must preserve each URI's richer fields; delayed enrichment must commit only to that URI and must not overwrite newer live fields.
- [ ] Run `bash test-native-windows.sh` and observe the enrichment regression before the fix.
- [ ] Correct both worker lookups, then rerun the Windows suite and existing rich-metadata tests. Retain rejection of ambiguous label-only lookup. Commit.

### Task 4: make publication outcomes preserve completed audio

**Files:** new `native/publication_job.h`, `native/audio_history.cpp` (`Publish`, `SaveWorker` and existing-file branch), `native/history_settings.*`, `native/file_publication.*` (narrow commit-permit seam), `tests/windows_quality_observer_test.cpp`, `tests/windows_publication_test.cpp`.

**Interfaces:** Replace `Saved/Skipped/Failed` with `PublicationResult`, carrying `PublicationOutcome {Saved, AlreadyPresent, Replan, RetryIO, Blocked, Cancelled}`, reason, and committed identity. The worker retains source ownership for Replan/RetryIO/Blocked; only acknowledged Saved/AlreadyPresent or explicit authorized cancellation releases it. Separate capture epoch, artifact revision and destination revision. Define `DestinationPermit` as the revision-checked linearization point immediately before promotion.

- [ ] Agree disable behavior, destination commit policy and AlreadyPresent/enrichment policy from the design before changing their semantics. Keeping the existing explicit capture-cancellation behavior is an available compatibility choice; benign settings changes must never imply cancellation.
- [ ] Add deterministic barriers after snapshot/enrichment, before promotion, and on the existing-file path. Toggle Lyrics, Never Pause, Auto Skip, speed and root; assert no completed audio disappears, no stale artifact commits, and exactly one logical output is acknowledged. Test disable/cancellation separately under the agreed policy.
- [ ] Run `bash test-native-windows.sh`; reproduce terminal disposal without timing sleeps.
- [ ] Implement explicit worker transitions and scoped freshness checks. A stale plan becomes Replan, an invalid source becomes visible Blocked, and a verified existing recording becomes AlreadyPresent by explicit policy. Do not hold settings locks across I/O or enrichment.
- [ ] Verify root changes before/after the permit, existing-file enrichment failures and commit-before-worker-acknowledgement. Preserve all raced-file and `.previous` recovery tests. Commit.

### Task 5: keep source, preparation inputs and artifact coherent

**Files:** `native/publication_job.h`, `native/audio_history.cpp`, `native/metadata_bridge.*`, `native/album_artwork.*`, `tests/windows_quality_observer_test.cpp`.

**Interfaces:** Add `PublicationPreferences`, `ResolvedPublicationMetadata`, `ArtifactKey` and `PreparedArtifact`. `ResolvePublicationMetadata(const Media&, const PublicationPreferences&)` returns a coherent catalog/tags/metadata revision/artwork identity snapshot or an explicit changed/wait outcome. `PreparedArtifact` atomically owns its key, catalog, tags, quality and bytes; `AudioSource` remains reachable independently. Destination policy is a separate key.

- [ ] After an injected write failure, change URI and track/disc to 7/2; retry must produce valid matching bytes and tags. With stable URI, change lyrics/artwork; retry must rebuild. Inject failed rebuild/allocation and assert source plus prior valid artifact remain recoverable.
- [ ] Run `bash test-native-windows.sh`; show stale-byte/catalog mismatch and stale enrichment before implementation.
- [ ] Snapshot metadata/tag preferences together and include artwork content identity or revision. Prepare a replacement locally, validate complete audio and identity, then swap the entire artifact. Never clear cached bytes if they are the only source; stop consuming the sole source through Flatten/tagging.
- [ ] Parse resulting FLAC/Ogg/MP3/AAC audio and tags, including stable-URI cases where MatchesRecording alone cannot detect stale enrichment. Run the Windows suite and commit.

### Task 6: process retries by earliest deadline

**Files:** new `native/retry_schedule.h`, new `tests/retry_schedule_test.cpp`, `native/audio_history.cpp::SaveWorker`, `test-native.sh`.

**Interfaces:** `RetrySchedule<Job>` offers `Push(Job, uint64_t deadline_ms)`, `PopDue(uint64_t now_ms)` and `NextDeadline()`. Order by monotonic deadline then insertion sequence; return ownership with the popped job. Preserve fresh/due-retry alternation in the worker.

- [ ] Fake-clock test: enqueue deadlines 30000 then 2000; at 2000 the second job runs. Test stable equal deadlines, recalculated wakeup, continuous fresh arrivals and job ownership on exceptions/shutdown.
- [ ] Run the new regression through `bash test-native.sh`; demonstrate current front-only ordering before replacing it.
- [ ] Integrate the schedule into the actual worker, retaining fresh-work notification and fairness. No real sleeps in scheduler tests.
- [ ] Run portable and Windows suites, including IndependentSaveProgress. Commit.

### Task 7: reserve construction peaks before allocation

**Files:** `native/compressed_buffer.*`, `native/aac_stream.h`, `native/aac_container.cpp`, `native/recording_limits.h`, `native/audio_history.cpp`, `tests/aac_container_test.cpp`, `tests/windows_playback_diagnostics_test.cpp`.

**Interfaces:** Introduce an overflow-checked `ConstructionMemoryPlan` with retained, incremental peak and final retained capacities, and a scoped reservation acquired before construction. AAC estimation receives external retained source capacity explicitly. Reserve blocks plus contiguous output during Flatten, source plus packets/config/tags/output during AAC packaging, and old plus replacement artifact during retagging; count each live allocation once.

- [ ] Add estimator boundary/overflow tests, caller-retained AAC source tests, and injected reservation denial before Flatten/mux/tag allocation. Failed mux must preserve a recoverable source; matched and unmatched paths must release/transfer reservations correctly.
- [ ] Run `bash test-native.sh` and `bash test-aac.sh`; confirm omitted external-source accounting in the baseline.
- [ ] Apply pre-allocation reservations and defer construction when unavailable. Do not free the source to satisfy the estimator; it is currently the retry recovery copy. Remove the misleading early-release comment.
- [ ] Run Windows plus large-podcast tests with allocation instrumentation. Verify no double charge/leak across retry, exception and cancellation. Report managed peaks separately from process RSS. Commit.

### Task 8: make unmatched destination attempts recoverable

**Files:** `native/publication_job.h`, `native/audio_history.cpp::PublishUnmatched`, `native/file_publication.*` (reuse existing transaction primitives), `tests/windows_playback_diagnostics_test.cpp`, `tests/windows_publication_test.cpp`.

**Interfaces:** `UnmatchedAttempt` records stable job/capture ID, destination revision, base, per-file state and pair commit state. A root change creates a new attempt rather than mutating old flags. A manifest/marker commits the verified audio/JSON pair last; cleanup accepts only artifacts whose identity/hash belong to that job.

- [ ] Inject failure before either write, after JSON, after audio, during marker commit and after commit before acknowledgement. Change root at every boundary; retry and pair-artifact recovery must recognize one logical committed pair with matching IDs/hashes. Full source/job restart recovery arrives in Task 10; partial files alone cannot recover audio that was still only in RAM.
- [ ] Run Windows tests to reproduce the old-root cached-path defect.
- [ ] Stage and flush the new pair, verify completeness/identity, commit its marker, then clean verified old artifacts. Preserve partial old artifacts until the replacement is committed; inaccessible cleanup remains pending.
- [ ] Rerun UnmatchedArchiveRetry, UnmatchedWriterRejectsPartial and transaction race/recovery tests. Assert no orphan sidecar is treated as a complete pair and no completed audio is deleted early. Commit.

### Task 9: explain matcher decisions with bounded evidence snapshots

**Files:** new `native/matcher_diagnostics.*`, `native/audio_history.cpp`, `native/async_log.*`, `tests/windows_playback_diagnostics_test.cpp`, `test-diagnostics.sh`.

**Interfaces:** `MatcherSnapshot` begins before exact/weak filtering or assignment, inventories retained metadata and competing captures, and accepts outcomes from the branches actually executed. `BeginSnapshot`, `RecordDecision` and `FinalizeSnapshot` never rerun matching or mutate assignment. Each snapshot has session-local IDs, coverage counts and a terminal reason when applicable.

- [ ] Add zero-candidate, assigned/unusable/out-of-window, equal-duration ambiguity, native mismatch/wait, delayed recovery, expired evidence, invalid audio and SaveUnmatched=0 cases. Assert first blockers, later checks unevaluated, unknown distinct from zero, changed evidence emitted and unchanged polls deduplicated.
- [ ] Run `bash test-diagnostics.sh`; demonstrate the missing inventory/final-discard coverage.
- [ ] Emit media summary followed by bounded candidate parts at existing LogLevel 4. Proposed collection caps: 32 metadata entries and 16 competing capture references per snapshot, 16 KiB serialized per snapshot, 2 MiB per session. Respect the existing 768-byte detail buffer with numbered multipart records. Emit totals, omitted counts and queue/drop/truncation markers; counters remain truthful when logging is saturated. Keep at most one latest changed snapshot per retained capture, charged to the managed budget.
- [ ] Exclude pointers, raw audio, credentials and personal paths. Default to session-local identity labels; bounded title/artist labels require a separate explicit debug-label setting defaulting off. Verify terminal snapshots occur before removal even when no sidecar is saved.
- [ ] Run diagnostic and Windows suites, test session exhaustion/logger saturation/privacy, and compare outputs with actual branch decisions. Commit.

## Stage B: durable recovery, after policy agreement

### Task 10: persist completed-job ownership and recover transactions

**Files:** new `native/pending_recordings.*`, `native/publication_job.h`, `native/audio_history.cpp`, `native/history_settings.*`, new `tests/pending_recordings_test.cpp`, new `tests/windows_pending_recordings_test.cpp`, native/Windows runners and INI documentation.

**Interfaces:** `PendingStore::CommitSource`, `Recover`, `AcknowledgePublication`, `Retry` and `Discard` operate on stable job IDs and versioned manifests. Lifecycle: AcceptedInRAM → Spooling → DurablePending → Preparing → Publishing → Committed/Blocked/Cancelled. CommitSource acknowledges only flushed, verified source and manifest; AudioSource can then become a durable handle. Integrate Task 8 destination progress rather than introducing another output transaction.

- [ ] Before implementation, agree the spool location, byte/job quota, free-space floor, capture admission/headroom, retry budget, disable behavior and commit permit policy listed in the design. Keep the 512 MiB default and five-start unresolved policy.
- [ ] Build fake-storage failure tests for create/write/flush/rename/ack/delete, quota reservations including staging bytes, oversized jobs, unavailable spool and invalid manifests. Add Windows child-process termination at each durability/ack boundary; recovered jobs must be idempotent and retain their original codec/identity.
- [ ] Run the new tests first and confirm the current RAM-only worker cannot recover a terminated completed job.
- [ ] Make spooling the first asynchronous completed-job operation, before network enrichment or output-root access. Stream compressed blocks without a whole-source Flatten. Persist completeness/configuration, association/correlation snapshot, source size/hash, policy/schema, attempts and destination progress. Commit source/manifest before releasing RAM; quarantine corrupt state visibly.
- [ ] Persist finite retry state and classify stale plan, resource wait, transient I/O, permanent destination failure and invalid source separately. Never spend I/O attempts while waiting for RAM. Do not silently evict completed jobs; refuse new capture admission visibly when commitments cannot fit. Graceful shutdown drains pending durability or reports inability to preserve it.
- [ ] Run portable fake-storage and real Windows restart/crash tests with full/unavailable destinations. Verify committed-but-unacknowledged outputs are recognized and source removal follows durable acknowledgement. Document the pre-spool crash window. Commit independently of UI changes.

### Task 11: expose recovery state and validate before release

**Files:** `native/pending_recordings.*`, To Disk menu owner located by source search, `native/history_settings.*`, default INI/README/wiki, Windows UI/integration tests.

**Interfaces:** `PendingStore::Status` returns a bounded immutable inventory with jobs/bytes, RAM/spool occupancy, affected recording and actionable blocked reason. Menu actions Retry, Change destination and Discard operate on stable IDs and acknowledged state; explicit discard is the only user action that intentionally removes an unsaved completed job.

- [ ] Add tests for exhausted retry budget across restart, manual retry, changed destination, explicit discard, unavailable source, saturated admission and concurrent status polling. Controls must not perform synchronous disk work on the UI/callback thread.
- [ ] Implement concise status/actions and approved settings copy. Keep normal logs actionable; detailed candidate/enrichment evidence stays at debug level.
- [ ] Run `bash test-native.sh`, `bash test-native-windows.sh`, `bash test-diagnostics.sh`, `bash test-capture-association.sh`, `bash test-aac.sh`, `bash test-large-podcast.sh` and existing dump replay/fuzz runners with their documented arguments. Verify supported formats by parsing/decoding actual outputs and comparing codec samples/frames/page continuity, not filenames alone.
- [ ] Replay preserved failing dumps and long-pause/resume, seek, rapid skip, three-second complete tracks, A → B → A, delayed metadata, equal-duration competitors, decoder reuse and mixed-codec playlists. Stitch only a source whose lifecycle and codec sequence/sample continuity prove no gap; otherwise retain separate incomplete/unmatched evidence.
- [ ] Build the candidate DLL, record its source commit and SHA256, install that exact build for user playback validation, and compare it to the eventual asset. Run multiple songs plus a non-news short podcast and a large podcast; inspect saves, metadata/quality, matched/unmatched inventory, counters, RAM and blocked-destination recovery.
- [ ] Obtain a fresh code review; correct and retest any changes. Only then push the implementation PR, merge after its checks, and prepare a new RC. Do not turn RC validation into a final 1.3 release without the user's release instruction.

## Execution order and handoff

Tasks 1 → 2 establish matching identity; Task 3 is independent. Tasks 4 → 5 define
publication ownership/artifacts; Task 6 is independent and Task 7 must account the
new artifact lifetimes. Task 8 depends on Task 4's destination policy; Task 9
observes the corrected matcher. Task 10 depends on the stable owner, accounting
and destination transactions; Task 11 completes recovery controls and validation.

Parallel read-only reviews and isolated implementation of Tasks 3/6 are reasonable.
Keep changes to the shared `audio_history.cpp` integration sequential. Avoid a
second matcher, second scheduler, second file-commit implementation or blanket
refactor. Delete replaced fields/helpers/tests only when their replacement's
production-path regressions pass.

## Execution status

Stage A is implemented. Focused regressions reproduced the reported conditional
paths before their fixes. Portable, Windows publication/matcher, large-file and
sanitizer checks validate the implementation; live playback is the next gate for
timing analysis. Timing defaults are unchanged.

Implementation uses the existing enrichment APIs with revision checks and an
exact content key, avoiding another metadata service. The existing file transaction
and codec readers remain in place. A failed optional diagnostic cannot unwind a
successful queue handoff.

The user deferred Tasks 10–11 (durable storage and its controls). No merge or
release is part of this correctness/logging pass.
