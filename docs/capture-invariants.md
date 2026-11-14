# Capture and publication invariants

Floggfy records the compressed bytes Spotify supplies. Decoded-output observations
correlate a decoder lifetime with a playback instance; they are evidence, not audio
and are never written to disk.

## Lifecycle

- A capture owns one codec stream lifetime. Decoder-address reuse starts a new
  lifetime. Compressed and decoded producers share a monotonic sequence, and
  decoded batches cannot coalesce across a lifecycle event.
- Compressed completeness and playback identity are independent observations.
  A validated complete FLAC source can bind to a unique native playback ID before
  PCM callbacks arrive. Duration-only fallback still requires decoded ownership.
  A Ready snapshot's listen generation is a valid local stage identity when the
  client omits its playback ID, but only after media kind and codec-declared
  duration select exactly one decoder capture. Wall clocks never establish or
  revoke ownership.
  Bounded record capacity may retire old evidence only after final reconciliation.
- A conflict retires the current-owner role but retains the conflicted record as
  ambiguity evidence. A later nonoverlapping capture can establish fresh ownership.
- Compressed completion may be processed before decoded observations. Producer
  sequence assigns delayed output to the exact completed lifetime, even when a
  decoder address is later reused. A newer begin sequence supersedes an older
  still-active reader for output attribution. Tied begins or a callback batch
  spanning a new begin remain ambiguous.
- FLAC completeness comes from contiguous compressed frames, header/frame CRCs,
  declared sample count, and exact buffer end. Native PCM coverage is diagnostic;
  a skip or missing callback cannot revoke complete compressed audio.
- A FLAC capture may contain decoder rereads. Capture-only normalization accepts
  repeated frames only when their bytes match an already verified frame at that
  sample position. Interrupted frames require a valid header and an exact prefix
  match against the corresponding complete frame. Missing samples, conflicting
  bytes, and unrelated containers cannot establish completion. Recovery stays
  within one capture, uses a bounded index charged to the shared RAM budget, and
  compacts in place only after full coverage. A fresh strict parse then validates
  the result before ownership completion. Short interrupted prefixes without a
  complete valid header remain conservatively unresolved.
- Recovery resynchronization retains its search position and retries only frame
  candidates that still need bytes. Exhausting the search window does not force
  rejection of a larger valid frame. Work counters distinguish repeated parsing
  from missing input. Exact native identity may transfer a metadata claim from a
  cleanly abandoned, incomplete FLAC restart chain to its verified complete
  replacement; completed, conflicting, lost, or unrelated lifetimes cannot transfer.
- A lost capture boundary prevents new identity binding for affected unresolved
  buffers. Complete bytes remain available for unmatched handling; a later ID on
  the same decoder cannot silently relabel them.
- Controls are consumed by the first Ready or buffering stage they can explain.
  Pause is acknowledged by paused Ready; play/resume cancels an unacknowledged
  pause. Seek/skip controls reject their transition without a clock window.
- A missing or stale media snapshot pauses new decisions but preserves already
  proven capture, ownership, and listen stages. Resumed position or an explicit
  lifecycle event performs reconciliation.
- Natural completion requires an ordered, playing instance transition, active
  control monitoring, no pending control, and one viable capture path. Elapsed
  wall time is not transition evidence.

## Capability policy

- Decoder layout discovery and an active decoded-output hook are separate states.
- Ownership is enforced when decoded observation is active. An app transition can
  supply URI/media kind while a Windows Ready snapshot supplies playback state and
  duration. Binding requires one decoded capture whose declared duration agrees
  with that Ready stage; a transition alone never assigns a decoder.
- Once that binding is proven, the app URI is the canonical listen identity across
  continuous Windows snapshots. A delayed title/artist correction cannot split the
  listen generation; a successor transition prevents carrying the old URI forward.
- If the optional decoded hook cannot be installed safely, capture continues at 1x
  without ownership gating. Accelerated menu choices are disabled.
- `CaptureSpeedLimit` is a restart-only visibility switch. Explicit `0` removes
  the Capture Speed submenu and disables acceleration. Missing values default
  to `0`; any valid nonzero value enables the submenu and all supported speeds.

## Formats and publication

| Media | Captured formats | Destination |
| --- | --- | --- |
| Music | FLAC, Ogg Vorbis, MP3, optional AAC/M4A | Music library layout |
| Podcast episode | FLAC, Ogg Vorbis, MP3, optional AAC/M4A | `Podcasts/<show>[/<year>]` |

- A completed candidate must pass cheap timing/duration checks and one cached
  structural validation before association. Publication validates the staged file
  again before atomic promotion.
- Owned MP3 completeness requires both valid MPEG framing and exact
  `HISTORY_SAMPLES` agreement. Frame-aligned or partial-frame truncation remains
  owned-but-incomplete so a later complete recording repairs the same path.
- Existing files are never replaced by filename alone. Identity, completeness,
  quality policy, exact-object locking, staging, and collision handling all apply.

- The ordinary destination resolver always runs first. If it finds no matching
  recording and a parsed candidate contains a different complete Spotify track
  URI, the same resolver is rerun below the library-root `Duplicates` folder.
  The first verified recording remains canonical. Duplicate filenames use the
  last 10 track-ID characters, expanding to 14 and then the full 22 only when a
  different complete URI occupies the shorter name. Full `SPOTIFY_URI` values,
  never filename text, decide identity. Same-URI recordings, URI-less legacy
  recordings, blocked reads, upgrades, and podcasts keep their established
  behavior and paths.
- Playback-start metadata filling, Auto Skip Downloaded, filename conversion,
  and the current-song folder action use that same final UID fallback. They do
  not infer identity from a filename or authorize replacement of the canonical
  conflict.
- Track metadata retains full `SPOTIFY_URI` and, when available, full
  `SPOTIFY_ALBUM_URI`. Album identity is provenance for manual review; it does
  not authorize replacement or change the ordinary folder layout.
- Episode enrichment uses an episode URI plus title/show identity and preserves
  publisher fields through the metadata cache and final native-format tags.

## Logging

`LogLevel` is the single general logging control:

| Level | Records |
| --- | --- |
| `0` | No log records |
| `1` | Newly finished tracks, warnings, and failures |
| `2` | Level 1 plus starts, upgrades, metadata updates, skips, renames, and settings activity |
| `4` | Level 2 plus diagnostic records |

Missing, duplicate, or invalid values normalize to level 1. Obsolete `Log` and
`DebugLog` values are ignored and removed when the authoritative `LogLevel` key
is added.

Every record uses local ISO 8601 time with a numeric UTC offset. The general log
is `Floggfy.log`; ordinary diagnostics use `Floggfy-diagnostics.log`; sanitized
network observations use `Floggfy-networking.log`; and callback/stall evidence
uses `Floggfy-hook-diagnostics.log`. `DebugNetworking` and
`DebugHookDiagnostics` are independent opt-ins that require level 4.
`DebugConnect` further controls sanitized Connect/control records in the main
diagnostic file. Log queues remain bounded and no lane records raw payloads,
credentials, URLs, media, device names, or device identifiers.

Capture IDs connect matcher decisions, conflicting decoder lifetimes, source and
decoded sample counts, validation reasons, retries, and final dispositions.
Unchanged native identity decisions are suppressed. With `DumpStreams=1`, decision
records also go to the session's `session.jsonl`, independent of rotating logs.
Matcher snapshots begin with a full inventory; subsequent `mode=delta` snapshots
name their `base_snapshot` and contain changed rows and explicit removals. Final
decisions always include a full snapshot. A missing base or part means incomplete
evidence: admission to a bounded log queue is not proof of delivery. FLAC reader
records include coverage, input/retained bytes, repeat counts, interruption bytes,
and specific rejection reasons; interruption counts are provisional until complete.
Their separate bounded queue cannot consume raw-input capacity; explicit decision
gap records report omissions. Both capture and publication workers finish their
diagnostic records before a graceful dump shutdown. Forced process termination
can still interrupt memory capture and logging.

With `DumpStreams=1`, `*-capture-N.bin` and `capture_event` rows separately record
the exact FLAC events accepted by the capture worker: original payload chunks,
frame callbacks, begin/end/error events, producer sequences, clocks, and applied
queue-loss, settings-reset, and outside-parser discard controls. They are not
the `*-stream-N.bin` consumed-decoder input.
Both use the existing bounded dump writer. `capture_gap` and write errors make
exact replay unavailable; diagnostic-only omissions do not invalidate capture
bytes. Capture-file metadata is explicitly unresolved, never an identity claim.

After Spotify closes the session, replay its buffer inputs with
`bash test-dump-replay.sh --capture-buffer SESSION.jsonl OUTPUT_DIRECTORY`.
The harness runs the same `ProcessFlacCapture` and queue-loss code, without
rechunking bytes or inventing frame callbacks. It validates and exports complete
buffers for comparison. This replays buffer behavior, not concurrent hook timing,
metadata matching, or disk publication; those have separate regressions. A replay
refuses missing capture events, incomplete journals, or absent payload bytes.

## Tests

`test-native.sh` covers portable parsers and correlation policy.
`test-native-windows.sh` covers Windows hooks, settings, menus, filesystem races,
format publication, and destination lookup on a Windows-capable local host.
CI runs the portable suite and sanitizer fuzzing on Ubuntu.
