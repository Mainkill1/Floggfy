# Metadata and publication

## Where metadata comes from

The native bridge injects [`metadata_collector.js`](../../native/metadata_collector.js)
into Spotify's main Chromium frame. The collector reuses already-created Spotify
client objects and the existing request builder. It emits bounded, versioned
messages through a reserved console prefix; it does not read credentials or replace
the client's network transport.

The collector supplies two related streams:

- **Playback state:** URI, title, artist/show, album, position, duration, playback
  instance, speed, local/buffering/pause state, and observed control actions.
- **Enrichment:** canonical album/episode fields, release date, identifiers,
  credits, label or publisher, genre, artwork URL, and optional lyrics.

[`metadata_bridge.cpp`](../../native/metadata_bridge.cpp) intercepts only the
reserved messages, validates them, and places them on bounded native queues. Rich
metadata is keyed by Spotify URI in a small RAM cache. Track and episode payloads
have separate accepted identity shapes; episode publisher/show metadata is kept.

## Building the recording identity

When a listen and capture are ready, Floggfy builds one canonical `Catalog` from
the live media identity plus cached enrichment. The same catalog drives:

- identity tags in the new recording;
- destination folder and filename;
- collision suffixes;
- duplicate comparison; and
- authorization to repair or upgrade an existing file.

This avoids deciding that two files are the same merely because their displayed
titles match. The recording also receives `HISTORY_*` evidence such as complete
listen, sample count/rate, source units/bytes, duration, elapsed coverage, capture
format, and save time. Tag construction begins in
[`audio_history.cpp`](../../native/audio_history.cpp).

## Tagging and metadata merge

Codec-specific writers add tags without changing the captured audio:

| Recording | Output | Metadata path |
| --- | --- | --- |
| FLAC | `.flac` | FLAC comments and picture blocks |
| Ogg Vorbis | `.ogg` | Vorbis comments and picture comments |
| AAC | `.m4a` | MP4/M4A atoms around original AAC packets |
| MP3 | `.mp3` | ID3 metadata around original MPEG frames |

For an existing complete Floggfy recording,
[`metadata_update_core.cpp`](../../native/metadata_update_core.cpp) fills missing
fields, prefers synchronized lyrics, reconciles date/year, and replaces artwork
only when the candidate is valid and demonstrably not worse. It does not overwrite
populated user fields just because enrichment arrived later.

Existing-file enrichment does not wait for another complete listen. Once playback
identifies the current track, the publication worker resolves and verifies any
owned complete recording already on disk. A metadata-cache revision—not elapsed
time—queues another bounded check when lyrics or other enrichment arrives later;
unchanged playback observations do not repeatedly scan or rewrite the file.

When better audio replaces older audio, publication preserves verified user
metadata and artwork from the exact locked old object. Incomplete owned recordings
are repair targets, but their unverified metadata is not trusted for preservation.

## Destination layout

[`library_layout.cpp`](../../native/library_layout.cpp) sanitizes Windows path
components and selects the media-kind layout. Common results are:

```text
Music\Artists\<album artist>\<album>\<title>.<format>
Music\Compilations\<album>\<title>.<format>
Music\Soundtracks\<album>\<title>.<format>
Podcasts\<show>\[<year>\]<episode>.<source format>
```

Reserved Windows names, invalid characters, trailing dots/spaces, and excessive
component lengths are handled before filesystem work. Optional track-number
prefixes affect filenames, not recording identity.

## Duplicate, repair, and upgrade decisions

Destination resolution enumerates deterministic ordinary, numbered, legacy, and
collision-suffixed candidates. Each existing candidate is inspected before a new
vacancy is selected. The result is one of:

- **New file:** no matching recording exists and a safe vacancy is available.
- **Skip:** the same or better complete recording already exists, or the existing
  object cannot be proven safe to replace.
- **Metadata update:** audio stays unchanged while missing verified metadata is
  added.
- **Repair/upgrade:** a matching owned file is incomplete, or the incoming quality
  is a verified improvement under the format policy.
- **Blocked:** an existing candidate cannot be read; retry later instead of making
  a possible duplicate.

When canonical lookup would allocate a collision suffix, publication instead
uses the root `Duplicates` hierarchy. This also covers unresolved collisions
without two valid Spotify track IDs. A valid incoming ID uses `[t-<id>]`;
an absent/malformed ID uses `[no-id-<hash>]`. Song lookup and track-start checks
use the same fallback. Existing canonical/legacy matches retain their paths.
Neither filename suffix authorizes a metadata merge, overwrite or upgrade.

FLAC can replace lower verified quality, including a complete owned Ogg for the
same recording. In that cross-format case, Floggfy locks the exact Ogg, carries
its retained metadata into the FLAC, verifies the published FLAC, and only then
retires the Ogg. A prior run that left both formats is repaired by the same path.
Vorbis compares supported nominal quality. AAC and MP3 do not infer that VBR size
variation means better quality. Format and identity rules live in [`recording_formats.h`](../../native/recording_formats.h),
[`library_layout.cpp`](../../native/library_layout.cpp), and
[`existing_quality.cpp`](../../native/existing_quality.cpp).

## Publication transaction

[`file_publication.cpp`](../../native/file_publication.cpp) treats planning as
advice, not write authorization:

1. Inspect the fully tagged incoming bytes again and require a complete matching
   recording with the expected quality.
2. Re-resolve the destination and lock the exact old file when upgrading.
3. Merge preserved metadata from that locked object.
4. Write a unique sibling staging file, flush it, and validate the transaction.
5. Promote without an overwrite flag. A pathname created by another writer wins.
6. For upgrades, reserve the old object as `.previous`, promote the staged file,
   then retire the backup only after success. Interrupted work remains recoverable.

Only a successful publication updates Latest Downloads and the current-item folder
action.

