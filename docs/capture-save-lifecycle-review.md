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
