# Capture/save correctness and recovery design

Status: proposed design; no runtime changes authorized by this planning document.

Reviewed source: main `4b559adb03328ed3db02f485b9c7a5fbd18d2968` (RC7).
Input: [PR #29](https://github.com/Mainkill1/Floggfy/pull/29), including its
[detailed review](https://github.com/Mainkill1/Floggfy/pull/29#issuecomment-6848493918),
at documentation head `7605ed43f0e0ece362845881d9f029444210fafd`.
The conditional defects below are source-confirmed, not individually reproduced
in Windows. Issue #27 has not been linked to a common cause.

## Intended behavior

Keep the stream-owned codec reader, independent occurrence history, and dedicated
matcher. The reader determines audio completeness; the matcher associates records;
the save worker owns publication and recovery. UI transitions and cleanup cannot
manufacture completeness, identity, or permission to discard a completed save.

1. Playback occurrences are distinct from catalog recordings. Without a native
   playback instance, A → B → A creates three occurrences. Repeated snapshots and
   pause/resume remain one occurrence. A backward seek alone proves no restart.
2. Retiring either side of an ambiguous comparison cannot create weak uniqueness.
   Preserve compact competitor evidence or a conservative lost-evidence fence.
   New exact native proof or a genuine contradiction may resolve ambiguity.
3. Enrichment with an exact URI is retrieved and merged under that URI. Labels
   alone cannot select between different URIs with identical text and duration.
4. An obsolete publication plan retains audio and replans. Explicit cancellation,
   successful publication, and a verified existing recording are distinct outcomes.
5. Source audio stays recoverable until a terminal acknowledgement. Prepared bytes,
   catalog/tags, metadata/artwork identity and preparation policy form one immutable
   artifact. A failed rebuild cannot destroy the only source or valid artifact.
6. Destination revision is separate from artifact revision. A root change usually
   replans a path; a lyrics/artwork change may rebuild bytes. Never Pause, Auto Skip
   and capture pacing do neither. An already committed file remains committed.
7. Retry order follows monotonic deadlines, with stable ties and fresh-work fairness.
   Memory is reserved before flattening, extraction, tagging or muxing, including
   simultaneously live source, blocks, old artifact, output and scratch allocations.
8. An unmatched job has a stable identity and destination attempts. Recover complete
   audio/JSON pairs by an explicit commit marker; partial artifacts are preserved
   until a verified replacement pair commits. Cleanup is verified and idempotent.
9. Detailed matcher diagnostics describe the real decisions and the retained
   candidate inventory before filtering. Unknown, omitted and unevaluated values
   remain distinguishable. Debug output is bounded, opt-in and asynchronous.
10. A later durable owner persists validated completed sources before enrichment or
    destination work. RAM is released only after durable acknowledgement. Output
    failure becomes a visible pending/blocked job with recovery controls.

## Constraints inherited from the existing design

- `MaxBufferedMiB=512` remains the default; its current range is 8–1024 MiB.
- `SaveUnmatched=1`; only intrinsically complete, validated audio is archived under
  the selected root's `Unmatched` folder. Raw `DumpStreams` remains separate.
- `[Matcher] MaxMatchQueue=5` counts subsequent observed playback starts, including
  repeated occurrences; pauses and elapsed time do not count. Range: 1–100.
- `TimeOffsetToleranceMs=10000` (0–120000) and `DurationToleranceMs=250` (0–2000)
  retain their names, units and defaults. End offsets remain diagnostic only.
- Timing/duration proximity is supporting evidence, never identity proof by itself.
- Preserve failed-enqueue ownership/counter restoration and the existing file
  transaction's flush, held-object identity, no-overwrite and `.previous` recovery.
- No disk I/O, network enrichment or unbounded diagnostic work in decoder callbacks.
- Preserve original codec payloads; do not concatenate streams from metadata alone.
- Require real Windows production-path tests and user playback testing before a new
  release. A portable test pass alone does not validate Windows publication.

## Alternatives and recommendation

**Logging and larger timing windows alone:** insufficient; they do not fix terminal
disposal, stale artifacts or disappearing ambiguity. Keep existing tolerances.

**Replace the capture pipeline:** unnecessarily broad. Existing codec readers and
file transactions already enforce important invariants; preserve them and repair
the ownership/identity boundaries around them.

**Recommended staged approach:** first correct identity, publication outcomes,
artifact consistency, retry scheduling, accounting and diagnostics. Then add a
durable pending-job owner as a separately reviewed change. Each stage has its own
regression and Windows gate; correctness work need not wait for spool policy.

## Proposed policies requiring agreement before the owning task

- Keep completed pending jobs when capture is disabled; stop admitting new capture.
  If compatibility requires cancellation instead, make it explicit and tested.
- Select destination at a revision-checked commit permit immediately before
  promotion. An acquired permit completes at that destination; later jobs use the
  new root. Do not hold the settings mutex during I/O or enrichment.
- Save valid audio with available metadata without waiting indefinitely for optional
  enrichment. Continue existing safe post-save metadata updates where supported.
- Use a persistent local spool independent of the selected output root, proposed
  under `%LOCALAPPDATA%\Floggfy\Pending`. This is recovery storage, distinct from
  user-visible stream dumps. Confirm location, disk quota and free-space reserve.
- Proposed transient-failure budget: seven total automatic attempts with waits of
  1, 2, 4, 8, 16 and 30 seconds, then visible Blocked. Persist the count over restart.
  Waiting for RAM or replanning does not consume disk attempts; permanent failures
  block promptly. Manual Retry grants another explicit budget.
- Never automatically expire/delete completed durable jobs. A full/unavailable
  spool blocks admission visibly and preserves already owned completed audio.
  Determine byte/job quotas and protected RAM headroom from large-podcast scenarios;
  do not introduce arbitrary smaller capture limits by splitting the 512 MiB pool.
- Retain the five-start unresolved policy; add no wall-clock expiry in this change.

A spool protects jobs only after its durable acknowledgement. Process termination
before that point can still lose RAM-only audio. Eliminating that window requires
incremental active-capture spooling, which is a separate design. An inaccessible old
destination can also prevent immediate physical duplicate cleanup; promise one
logical committed destination and eventual verified cleanup, not impossible atomic
renames across unrelated roots.

## Deliberate exclusions

No fingerprint dependency, new scoring matcher, general codec rewrite or automatic
stream stitching. Test continuation, sequence/sample gaps and lifecycle boundaries
against dumps first. Fingerprinting, if later desired, cannot itself establish an
exact Spotify URI or recording edition. Do not attribute issue #27 to these defects
without a reproducer.
