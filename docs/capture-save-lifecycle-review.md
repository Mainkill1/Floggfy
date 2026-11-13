# Capture/save lifecycle review

Status: discussion draft. Documentation only; no runtime behavior changed.
Reviewed against main `4b559adb03328ed3db02f485b9c7a5fbd18d2968` (RC7).
Findings below are static code observations, not reproduced failures.

## Current behavior and concerns

- **Shared memory pressure:** capture allocation defaults to **512 MiB** (configurable 8–1024 MiB). Active, retained and publishing/retry allocations share the budget. When additional compressed blocks cannot fit, the receiving FLAC/MP3/AAC or ordinary active Ogg recording is discarded, including previously captured bytes. This is not an oldest-completed-job eviction policy. Retained Ogg late-tail handling instead keeps prior bytes and marks the recording ambiguous.
- **Persistent save failures:** completed jobs remain in RAM and retry indefinitely, with delays capped at **30 seconds**. Their reservations reduce room for incoming audio. A persistently unavailable destination could therefore cause later captures to be dropped while earlier saves keep retrying. This impact needs testing; it is not evidence of an unbounded RAM leak.
- **Unresolved retention:** unqueued recordings and metadata retire by subsequent playback starts (`MaxMatchQueue=5` by default), not elapsed time. If playback stops advancing, unresolved records need not age out. Incomplete/invalid audio is discarded on retirement; complete unmatched audio may be saved under `Unmatched` with a correlation JSON when `SaveUnmatched=1` (default).
- **Visibility/recovery:** memory failures are logged at `LogLevel >= 1`, without a dedicated recovery prompt in these paths. A discarded capture does not resume midway; a fresh source lifecycle can capture once budget is available. Already queued valid saves remain paired with their metadata.

## Decisions to work through

- [ ] Define active-capture versus pending-save budgets and admission/backpressure behavior.
- [ ] Classify transient/persistent write failures and expose retry/change-destination/explicit-discard controls. Evaluate bounded durable spooling or a visible blocked state; do not silently delete completed recordings.
- [ ] Decide whether unresolved records need an idle-retention policy. Age alone must not establish invalid audio or a metadata match.
- [ ] Expose pending bytes/jobs, blocked reason and affected recording when capture is dropped.
- [ ] Test repeated skips, blocked/full destinations, mid-track allocation failure, and recovery after the destination becomes writable. Verify association integrity and completed-recording preservation.

The two-slot publication queue already restores capture ownership on a failed enqueue. Preserve that behavior. The configured budget is not a strict whole-process RAM ceiling: output construction and other allocations also exist.

## Code locations

All line references use the reviewed commit:
- [`audio_history.cpp`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/audio_history.cpp): `SaveWorker` 1715–1759; allocation checks 2338–2343, 2465–2477, 2507–2525, 2627–2634; `QueueRecording` / `ScheduleStreamPublications` 2805–2868; separate active-lifecycle caps 3077–3093.
- [`recording_metadata.h`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/recording_metadata.h): `RecordingHistory::Observe` and `Prune`.
- [`compressed_buffer.cpp`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/compressed_buffer.cpp): `CompressedBuffer::Append`.
- [`async_log.cpp`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/async_log.cpp): `LogEvent`.
- [`recording_limits.h`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/recording_limits.h) and [`history_settings.cpp`](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/history_settings.cpp): budget and matcher defaults/ranges.

## Related reports

[#27](https://github.com/Mainkill1/Floggfy/issues/27) reports publication/unmatched failures, but its cause has not been linked to this lifecycle concern. Older #20/#22/#23 concern transition/identity failures. No direct lifecycle/backpressure duplicate was found in the issue search.


## General review: source-confirmed conditional defects

These paths are verified in source; the triggering scenarios below have **not** been reproduced in the Windows application.

### Completed capture discarded when an unrelated setting changes during publication

- **Trigger/impact:** while a new recording is being resolved/tagged, change Lyrics, Auto Skip, Never Pause, capture speed, or Save Location. These can increment the settings generation without invalidating the capture epoch. The final generation check returns `Publication::Skipped`; the save worker then destroys the completed capture rather than retrying it. This is a timing-dependent lost save, not the deliberate cancellation caused by disabling capture.
- **Locations:** [`Publish` final check, lines 1450–1451](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/audio_history.cpp#L1450-L1451); [terminal job disposal, lines 1752–1755](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/audio_history.cpp#L1752-L1755); [non-capture settings updates, lines 440–496](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/history_settings.cpp#L440-L496).
- [ ] Separate an obsolete publication plan from an invalidated capture. Preserve/requeue the audio and rebuild any affected tags/path for benign setting changes; retain explicit cancellation for disabled capture/formats.
- [ ] Regression: change Lyrics or Save Location between the initial settings snapshot and final publication check; assert one completed recording remains pending or saves under the current policy, with no stale path/tag publication.

### Changing Save Location does not redirect pending unmatched retries

- **Trigger/impact:** an unmatched save fails after its base path is assigned, then the user selects a working destination. The retry keeps `capture.unmatched_base` under the old root, while directory creation uses the new root. It continues failing against the old unavailable location, or writes there if it becomes available; changing destination does not recover that job.
- **Locations:** [cached base and retry writes, lines 1676–1710](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/audio_history.cpp#L1676-L1710); [`SetSaveLocation`, lines 488–496](https://github.com/Mainkill1/Floggfy/blob/4b559adb03328ed3db02f485b9c7a5fbd18d2968/native/history_settings.cpp#L488-L496).
- [ ] Track the destination generation for unmatched jobs and define explicit relocation/retry behavior. Preserve any already-written JSON/audio pair; do not simply reset completion flags or delete the old artifact.
- [ ] Regression: fail before either file writes and separately after the JSON writes, change root, then verify recovery produces a consistent audio/sidecar pair without duplication or loss.

## Review verification and remaining scenarios

- `bash test-native.sh` passed (exit 0) on the reviewed main commit. This covers the portable suite, not a live Spotify session or the Windows publication worker.
- Windows production-path tests were not run: this Linux review environment has neither the MinGW compiler nor a Windows/Wine runner.
- No direct duplicate of the two conditional defects above appeared in the repository issue search; no link to #27 is established.
- [ ] Buffer-swap/stitching behavior remains **needs deeper testing**, not a confirmed missing implementation. Test same-decoder continuation versus a genuinely new lifecycle, including partial sources; do not infer safe concatenation from matching metadata alone.
- [ ] Exercise rapid skipping with delayed metadata and a playlist of complete three-second tracks. Check exact native identity separately from weak timing/duration ambiguity, metadata freshness, queue pressure and retention.

## Requested diagnostic plan: explain failed song matches

**Existing coverage:** `ReportMatcher` (`audio_history.cpp` 2673–2695) already logs capture/metadata IDs, timing and duration deltas, tolerances, completeness and selected mismatch/ambiguity outcomes at `LogLevel >= 4`, deduplicated by pair/reason. Native identity and ownership diagnostics also exist (766–827, 2002–2085, 2203–2226). `UnmatchedCorrelation` (2786–2804) includes retained metadata labels/IDs/durations, but only supplies the sidecar for a valid unmatched save.

**Gap:** there is no consolidated failed-match candidate inventory. Several early exits have no pair-specific explanation: unusable/already-assigned metadata, lost evidence, waiting for native identity, excluded capture/ownership states, missing declared duration, and unsuccessful binding. Existing pair messages can be scattered or suppressed after the first occurrence; the sidecar does not explain each candidate's eligibility or prove what evidence existed at the decision.

- [ ] Emit one correlated failure snapshot before unresolved retirement/discard, including when `SaveUnmatched=0` or audio is incomplete. Distinguish temporary waiting/ambiguity from final retirement; do not call every polling pass a failure.
- [ ] Enumerate retained metadata candidates for the affected capture and identify competing captures behind ambiguity. Record candidate IDs, considered/excluded status, exact-native versus weak-match path, and the actual rejection/waiting reason. Include missing/expired evidence explicitly; never imply an omitted candidate did not exist.
- [ ] Include the decision's capture epoch/token, metadata generation, codec/completeness, native identity agreement and observation window, ownership state/reason, assignment state, first-observed/estimated-start/duration deltas and configured tolerances, candidate counts, and retirement/loss reason. Reuse the real matcher decisions rather than a second diagnostic matcher; record unknown values as unknown, not zero. This should show whether an apparent match was excluded, ambiguous, or genuinely overlooked, without loosening matching rules.
- [ ] Keep detailed candidate snapshots opt-in with existing debug logging. Use session-local correlation IDs by default; make bounded title/artist labels optional for local investigation. Exclude credentials, raw audio, process memory/pointers and personal file paths; do not upload logs automatically.
- [ ] Bound records, bytes and queue use per snapshot and per session, with explicit total/emitted/omitted counts and truncation/drop markers. Deduplicate unchanged evidence, emit changed decisions plus the final outcome, use the existing asynchronous logger, and avoid decoder-callback disk I/O. Do not generate an unbounded all-pairs dump.
- [ ] Test zero candidates, equal-duration ambiguity, native-ID mismatch/wait, assigned metadata, delayed metadata recovery, unavailable/expired evidence, invalid audio and `SaveUnmatched=0`; assert candidate reasons, no false match, no repeated unchanged spam, and predictable truncation/privacy filtering.

**Modify:** `audio_history.cpp` functions named above plus `AssociateRetainedRecordings` (2696–2784), `ScheduleStreamPublications` (2822–2868) and discard paths; reuse `async_log.cpp::LogEvent` and its bounded queues. Add production-path diagnostics regressions alongside `tests/windows_quality_observer_test.cpp`. Plan only; no logging implementation or new runtime tests added in this documentation update.

### Collection design: include candidates before exclusion

1. On the capture worker, start a diagnostic snapshot at the beginning of `AssociateRetainedRecordings`, **before** either exact/weak matching loop can filter or assign records and before `RecordingHistory::Prune`. Enumerate `history.Records()` into an immutable, bounded snapshot keyed by metadata generation, including already-assigned, unusable and out-of-window records. Snapshot capture token/epoch and relevant competing capture IDs at the same decision point.
2. Instrument the **production** matching branches to attach outcomes to those entries: evaluated/eligible, excluded with reason, waiting for evidence, competing/ambiguous, selected, or not evaluated because an earlier gate stopped that path. Preserve the first blocking reason and the facts used there. Never rerun binding/assignment for logging, invent unexecuted results, or treat timing proximity as identity proof.
3. Each entry carries the observed metadata identity/correlation IDs, observation time, estimated start, declared duration, assignment state, and available native-ID/ownership agreement. Pair-specific results record capture timing/duration deltas, effective tolerances and competing IDs/counts. Link every emitted entry to a snapshot ID plus capture token/epoch so the inventory and decision evidence cannot be confused with a later poll.
4. Keep only a bounded latest/evidence-changed diagnostic snapshot per retained capture while debug collection is enabled; finalize it on unmatched retirement/discard, before removing the capture or metadata. Earlier emitted snapshots can explain a later failure when evidence has since expired. These records describe evidence available at each snapshot, not every possible historical song.
5. Record inventory total, emitted count, omitted count/reason, retention window, known prune/queue-loss counters and unavailable history. Use explicit `not-retained`, `not-observed` or `unknown` coverage indicators only when supported. **Metadata already pruned before collection or never observed cannot be reconstructed**; complete coverage cannot be claimed after truncation or queue loss. Do not retain an unbounded second history to conceal that limit.
6. Add regressions proving pre-filter inventory includes assigned/unusable/out-of-window candidates, a late-arriving candidate appears in a newer snapshot, and pruning/output caps visibly reduce coverage without changing matcher decisions. Preserve the opt-in, byte/record budgets and privacy controls above.

This collects all currently retained metadata candidates before exclusion **within the stated diagnostic limits**; “all possible matches” is not a promise to recover missing history or force a match.

### Proposed record layout: media first, then each metadata possibility

Illustrative output only, not an existing log schema or a reproduced incident. Emit the unmatched media summary first, followed by one bounded entry for **each retained metadata possibility**, including excluded entries.

```json
{
  "snapshot_id": "s42",
  "unmatched_media": {
    "capture_id": 81,
    "capture_epoch": 3,
    "codec": "ogg",
    "duration_seconds": 180.0,
    "source_bytes": 4200000,
    "complete": true,
    "native_identity_ref": null,
    "native_identity_status": "unavailable",
    "capture_start_monotonic_seconds": 1000.0,
    "outcome": "unmatched_retired",
    "subsequent_observed_playback_starts": 5,
    "retirement_threshold": 5
  },
  "coverage": {
    "retained_metadata_total": 2,
    "emitted": 2,
    "omitted": 0,
    "known_pruned_metadata_count": 3,
    "prior_history": "not_retained",
    "unobserved_metadata": "unknown"
  },
  "metadata_candidates": [
    {
      "metadata_generation": 17,
      "identity_ref": "m17",
      "duration_seconds": 175.0,
      "estimated_start_monotonic_seconds": 1001.0,
      "assigned": false,
      "path": "weak",
      "comparisons": [
        {"check": "start_window", "absolute_delta_ms": 1000, "limit_ms": 10000, "result": "pass"},
        {"check": "duration", "absolute_delta_ms": 5000, "limit_ms": 250, "result": "reject"}
      ],
      "decision": "excluded",
      "reason": "duration_mismatch",
      "later_checks": "not_evaluated"
    },
    {
      "metadata_generation": 18,
      "identity_ref": "m18",
      "duration_seconds": 180.0,
      "estimated_start_monotonic_seconds": 1000.0,
      "assigned": true,
      "comparisons": [
        {"check": "already_assigned", "result": "reject"},
        {"check": "start_window", "result": "not_evaluated"},
        {"check": "duration", "result": "not_evaluated"}
      ],
      "decision": "excluded",
      "reason": "metadata_already_assigned"
    }
  ]
}
```

Identity references are session-local diagnostic labels; optional local title/artist labels may help inspection under the privacy controls above. Comparisons must reflect checks actually executed, with observed values and tolerances; matching-looking fields are not proof of an overlooked match when an earlier gate prevented evaluation. Populate prune/loss counters only if instrumentation knows them; otherwise use unknown, never a fabricated zero.

**Five is not a candidate count:** default `MaxMatchQueue=5` is the retirement threshold measured in subsequent observed playback starts. It does not promise five metadata candidates, five seconds, or five Next-button clicks. Enumerate the actual retained collection, explicitly marking pruned/missing history and any diagnostic truncation.
