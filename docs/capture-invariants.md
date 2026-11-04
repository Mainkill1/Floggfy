# Capture and publication invariants

Floggfy records the compressed bytes Spotify supplies. Decoded-output observations
correlate a decoder lifetime with a playback instance; they are evidence, not audio
and are never written to disk.

## Lifecycle

- A capture owns one codec stream lifetime. Decoder-address reuse starts a new
  lifetime. Compressed and decoded producers share a monotonic sequence, and
  decoded batches cannot coalesce across a lifecycle event.
- Correlation advances through observed stages: capture created, decoded output
  seen, playback identity bound, compressed stream complete, then publishable.
  A Ready snapshot's listen generation is a valid local stage identity when the
  client omits its playback ID, but only after media kind and codec-declared
  duration select exactly one decoder capture. Wall clocks never establish or
  revoke ownership.
  Bounded record capacity may retire old evidence only after final reconciliation.
- A conflict retires the current-owner role but retains the conflicted record as
  ambiguity evidence. A later nonoverlapping capture can establish fresh ownership.
- Compressed completion may be processed before decoded observations. Producer
  sequence assigns delayed output to the exact completed lifetime, even when a
  decoder address is later reused.
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

## Formats and publication

| Media | Captured formats | Destination |
| --- | --- | --- |
| Music | FLAC, Ogg Vorbis, optional AAC/M4A | Music library layout |
| Podcast episode | MP3 | `Podcasts/<show>[/<year>]` |

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
  The duplicate filename retains the complete track ID. Same-URI recordings,
  URI-less legacy recordings, blocked reads, upgrades, and podcasts keep their
  established behavior and paths.
- Playback-start metadata filling, Auto Skip Downloaded, filename conversion,
  and the current-song folder action use that same final UID fallback. They do
  not infer identity from a filename or authorize replacement of the canonical
  conflict.
- Episode enrichment uses an episode URI plus title/show identity and preserves
  publisher fields through the metadata cache and final MP3 tags.

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

## Tests

`test-native.sh` covers portable parsers and correlation policy.
`test-native-windows.sh` covers Windows hooks, settings, menus, filesystem races,
format publication, and destination lookup on a Windows-capable local host.
CI runs the portable suite and sanitizer fuzzing on Ubuntu.
