# Implementation walkthrough and optimization review

Normal recording crosses four boundaries. Spotify supplies compressed audio and
playback observations. The capture worker associates a complete recording with
the observed playback. The save worker resolves and verifies the file on disk.
The UI reads the current result and sends commands through those same services.

## Startup and compatibility

The loader exposes the Windows version APIs Spotify expects. It forwards those
calls to the system DLL and starts a worker that reads the INI and waits for the
Spotify and Chromium components it needs. The worker activates each supported
feature and keeps the app responsive when an optional component is unavailable.
The separate launcher loads the same Floggfy library for that install mode.

Startup and [operations](operations-and-safety.md) are described in
[version_proxy.cpp](../../native/version_proxy.cpp) and
[startup_launcher.cpp](../../native/tooling/startup_launcher.cpp).

## Media input and format validation

Audio callbacks submit bounded observations. The capture worker owns codec
buffers and organizes the compressed bytes by decoder lifetime. It follows Ogg
pages, FLAC frames, MPEG frames, and AAC packets using format-specific sequence,
structure, and sample-count checks. Decoded output can show that the decoder
played audio and provide timing and quality evidence. The recording keeps the
original encoded audio. AAC packets are wrapped in M4A for publication.

A pause can leave a stream open. A seek, a buffer loss, or a fresh decoder
lifetime changes the evidence the worker has to validate. Those events cannot
substitute for validation of a complete stream. [Capture and correlation](capture-and-correlation.md),
[audio_history.cpp](../../native/media/audio_history.cpp).

## Playback and matching

The renderer collector reports URI, playback instance, duration, position, and
states such as buffering, paused, or local playback. The native bridge validates
each message before passing it to the worker. The optional Windows media-session
fallback can supply playback observations when the preferred client state is
unavailable.

The matcher tracks compressed captures and playback history independently. It
prefers an exact identity supported by decoder ownership. When that evidence
is unavailable, it checks compatible timing and duration and requires a unique
candidate. It retains completed recordings long enough for delayed playback
evidence to arrive. A complete recording that ages out after the configured
number of media starts can be saved under `Unmatched`. An incomplete stream
fails that archive gate. The active and completed capture lists use the same
archive decision. Active entries also release decoder-specific state when they
retire. [client_playback.h](../../native/media/client_playback.h),
[decoder_ownership.cpp](../../native/media/decoder_ownership.cpp),
[capture invariants](../capture-invariants.md).

This pass replaced temporary maps of timeline and transition fields with slots
that refer to the original payload. The parsers still reject duplicate keys,
invalid URIs, malformed clocks, and inconsistent flags. On 10,000 parses of a
representative timeline fixture, heap allocations went from 330,000 to 210,000.
For 10,000 parses of a buffering-transition fixture they went from 160,000 to
30,000. These are parser measurements. Live playback timing was outside the
measurement.

## Metadata, artwork, and lyrics

The metadata record stays alive while a capture or save job uses it. The bridge
picks up available Spotify fields and artwork references first. Enabled fallbacks
can read the disk cache or request missing information. A worker does those
reads and requests and checks which metadata revision owns the result before
publishing it. SQLite supports Floggfy's own Saved catalog. The separate LevelDB
reader serves Spotify's on-disk metadata cache. Cached WebP artwork can be
converted to PNG before embedding.

Tracks and episodes share recording identity checks while supplying different
metadata fields, including album details for tracks and show, date, and artwork
when available for episodes. [Metadata and publication](metadata-and-publication.md),
[metadata_bridge.cpp](../../native/media/metadata_bridge.cpp).

## Save worker and library

The save worker takes completed jobs, validates the encoded data, builds tags,
and uses the same recording identity to resolve ordinary, numbered, alternate,
and duplicate paths. It can skip an existing complete match, update missing
metadata, or upgrade an owned file when quality and format rules allow. It writes
and flushes a staging file before claiming the destination. If the destination
is locked or changes, the worker can retry or replan without losing the completed
capture. A same-format upgrade validates one locked snapshot and uses it for the
metadata merge. Memory accounting includes the retained source and scratch
allocations for validation, tagging, and publication. [file_publication.cpp](../../native/media/file_publication.cpp),
[library_layout.cpp](../../native/media/library_layout.cpp).

## To Disk and Saved

To Disk reads a playback and quality snapshot. A separate worker checks whether
the song has a valid saved file. Menu callbacks use that result for the Song
shortcut and recheck the path when opening Explorer. The menu also writes the
same preferences used by the workers.

The Saved column queries a SQLite index under Save Location. The catalog verifies
file evidence and reads recording tags when the on-disk stamp has changed. The
renderer caches returned dates, displays them on mounted rows, and uses the full
playlist order when playing from a Saved sort. **Marked DB Rebuild** scans the
library and can backfill missing save dates from file creation time when the
metadata settings allow it. The release builds SQLite for size and leaves out
JSON functions and shared-cache mode. The app's schema uses ordinary indexed
tables and one worker-owned connection. [to_disk_menu.cpp](../../native/ui/to_disk_menu.cpp),
[marked_catalog.cpp](../../native/ui/marked_catalog.cpp),
[marked_playlist_sort.js](../../native/ui/marked_playlist_sort.js).

## Telemetry and user diagnostics

The optional telemetry module classifies known reporting destinations and honors
each enabled category. Native and renderer observations contribute to the block
counts. Opt-in dumps use a bounded queue and format payloads off the callback
path. Queued-telemetry cleanup handles known pending files at startup and normal
exit. Coverage depends on the request paths the running client exposes.

Ordinary logs report outcomes and warnings. The matcher log explains how
candidates were evaluated. Stream dumps preserve raw chunks and metadata for
replay, and telemetry dumps preserve blocked request details. These are features
of the shipped DLL with runtime controls. [Telemetry controls](../telemetry-testing.md),
[async_log.cpp](../../native/tooling/async_log.cpp),
[stream_dump.h](../../native/media/stream_dump.h).

## Shared tooling and release build

The tooling directory holds the bounded queues and retry schedule, INI parsing
and persistence, Windows file operations, database access, and binary and text
readers. Keeping those operations shared lets the media, UI, and telemetry
modules use the same mechanics while choosing their own input limits and
recovery policy. The release build embeds the defaults and renderer scripts.
Fixtures, fault injection, fuzzers, and replay runners live under `tests/`.
[Component map](README.md#component-map),
[release and development builds](../../AUDIO-HISTORY.md#release-build).

## Where to look for more savings

| Area | Evidence to collect and work to remove |
|---|---|
| Capture and matcher | Count full passes over retained captures and candidate comparisons during skips and a large episode. Check whether playback or capture changes can trigger re-evaluation in place of some repeated passes. Late metadata and final decoder output must still trigger work. |
| Save worker | Measure peak scratch memory during a large quality upgrade. The locked snapshot now serves both validation and the metadata merge. The merge still copies the incoming bytes through the codec tagging interface. |
| Saved column | Separate SQL time, file checks, and DOM updates on a large playlist. The catalog already reuses prepared statements. The measurements can establish whether batching more SQL or reducing UI work will help. |
| Enrichment | Log the path from live metadata to disk cache to network fill for missing artwork and lyrics. Compare work per recording and follow the cache read budget when checking any reduction. |
| Startup and telemetry | Measure startup duration and opt-in dump decode time independently of audio recording. Profile only paths exercised by enabled features. |
