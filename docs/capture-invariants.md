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
  Wall clocks expire unresolved evidence; they do not establish ownership.
- A conflict retires the current-owner role but retains the conflicted record as
  ambiguity evidence. A later nonoverlapping capture can establish fresh ownership.
- Compressed completion may be processed before decoded observations. Producer
  sequence assigns delayed output to the exact completed lifetime, even when a
  decoder address is later reused.
- Pause is acknowledged by a paused observation for the same playback instance.
  It cannot remain pending through resumed playback and poison a natural ending.
- Seek/skip controls still reject listen coverage. Natural completion requires an
  observed, playing instance transition and no unacknowledged recent control.

## Capability policy

- Decoder layout discovery and an active decoded-output hook are separate states.
- Ownership is enforced only when decoded observation is active and the playback
  provider supplies an instance ID. Windows media fallback still uses the existing
  timing, duration, ambiguity, and final-integrity checks.
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
- Episode enrichment uses an episode URI plus title/show identity and preserves
  publisher fields through the metadata cache and final MP3 tags.

## Tests

`test-native.sh` covers portable parsers and correlation policy.
`test-native-windows.sh` covers Windows hooks, settings, menus, filesystem races,
format publication, and destination lookup on a Windows-capable local host.
CI runs the portable suite and sanitizer fuzzing on Ubuntu.
