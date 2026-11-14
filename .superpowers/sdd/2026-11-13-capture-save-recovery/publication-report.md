# Immediate publication correctness report

Scope: Tasks 4/5/8, with the publication-side construction calls for Task 7. Root owns the shared `audio_history.cpp` commit and the retry/global-memory integration. Durable pending storage is deliberately deferred.

## Behavior

- Publication now distinguishes Saved, AlreadyPresent, Replan, RetryIO, Blocked and Cancelled. The worker retains audio for stale plans, I/O failures and blocked construction/validation. Existing capture-disable/epoch cancellation remains explicit.
- Lyrics and destination changes carry independent monotonic revisions, so change-and-return during preparation cannot evade freshness checks. Never Pause, Auto Skip and pacing generation changes leave artifact/destination authorization intact.
- File publication accepts an optional permit after flush/staging and immediately before promotion. A rejected permit leaves the exact old object and transaction recovery behavior intact; an acquired permit may finish at its chosen destination.
- A complete matching existing recording can acknowledge independently of optional metadata success. The current held file is revalidated before acknowledgement; a raced foreign destination causes Replan.
- The immutable prepared artifact contains its content key, metadata revision, catalog, tags, quality and bytes. Exact cover content participates in the key. Source bytes remain independently reachable through failures/rebuilds. A replacement is built and validated locally before swapping the old artifact.
- Unmatched jobs keep stable capture identities and bounded destination attempts keyed by destination revision. Each attempt writes/verifies JSON and audio, holds both objects against writes/deletion, then flushes a hash/identity commit marker last. A changed root preserves old partial artifacts until a replacement commits. A committed pair is recovered before acknowledgement, even after a root change. Cleanup verifies the held object, preserves foreign replacements and stays pending when inaccessible.
- Non-AAC construction uses an allocation-free codec-specific estimate: replacement output plus metadata/packet scratch, rather than a multiple of the entire audio payload. FLAC/Ogg reserve enough output for all incoming text; MP3 reserves its final result. The 180 MiB estimator cases fit the existing 512 MiB pool without allocating a large fixture. AAC source/prior-artifact capacities are supplied to the new AAC planner.

## Regression evidence

Observed red before corresponding fixes:

1. Deterministic Auto Skip change inside enrichment returned terminal Skipped instead of preserving/saving the complete FLAC fixture.
2. Injected publication failure left `publication_source` empty: prepared output had consumed the independent recovery source.
3. Lyrics off/on during enrichment evaded value-only freshness checks and failed the Replan assertion.
4. Isolated old acknowledgement behavior (only the final raced-file revalidation removed) returned AlreadyPresent for a foreign file created between resolution and publication. The corrected fixture returns Replan and saves in a safe duplicate destination.

Focused Windows fixture coverage includes URI/track/disc changes (7/2), stable-URI lyrics/artwork changes, failed rebuild retaining the old artifact plus source, optional existing-metadata failure, stale root/artifact revisions, disable cancellation, unmatched root changes after JSON/audio/commit, a return to the old root, inaccessible cleanup, and an exception after pair commit before acknowledgement.

## Verification

- Focused MinGW quality-observer build and Windows execution: passed in the final repeat, including destination-return, interrupted acknowledgement, inaccessible cleanup and raced foreign destination cases.
- Focused MinGW file-publication build and Windows execution: passed; includes existing staged races, locks, `.previous` recovery, child-process termination, denied permits and completion after a granted permit.
- `g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/tag_memory_plan_test.cpp` and execution: passed.
- `bash test-native.sh`: an earlier complete run passed. A later run was interrupted by a concurrent shell-script edit (`riginal: command not found`); the next run reported two in-progress matcher-analysis tests failing (generation grouping and missing rejected duration statistics). Both were reported to root, who owns those changes and reruns the frozen integrated suites.
- `git diff --check`: passed before final staging.

## Limits

Completed jobs still live in RAM until acknowledged. This change does not survive process loss before durable spooling. Inaccessible cleanup can retain the job/source until it succeeds. Destination-attempt bookkeeping is capped at 16; reaching the cap blocks while preserving the source. Estimates bound managed construction memory and do not represent process RSS. Live Spotify playback and release gates remain root/user work.
