# Auto Skip Downloaded Design

## Goal

Add a default-off **Auto Skip Downloaded** option directly below Lyrics in the To Disk menu. When enabled, Floggfy may advance a locally playing Spotify music track only after it has verified a complete Floggfy-owned copy on disk and finished the existing-file metadata check.

## Decision stages

1. Spotify supplies a music URI, playback instance, and current local playback state. Windows media state is never sufficient for this feature.
2. The existing-file worker resolves the recording using the normal bounded directory and identity checks. Missing, unreadable, incomplete, unowned, mismatched, or podcast files stop the decision.
3. The worker completes the metadata check. A required metadata update that fails stops the decision; no skip is issued.
4. The current quality snapshot must belong to the same media identity and expose a proven codec, sample rate, channel count, and (for FLAC) bit depth. Unknown or stale quality waits for a later Spotify observation rather than guessing.
5. The shared quality policy decides whether the proven current FLAC would upgrade the saved recording. If it would, playback continues. Otherwise native code queues one request for that playback instance.
6. The injected Spotify collector accepts the request once, rechecks the exact Spotify URI and local playback target, and invokes Spotify's Player `next` command. A stale, disabled, remote, paused, missing-player, or identity-mismatched request does nothing.

## State and retries

No elapsed-time threshold decides whether to skip. Metadata revisions, quality evidence, settings generation, Spotify URI, and playback instance are the stages.

The existing-check deduplication key includes the playback instance and quality evidence while this option is enabled. This allows a first unknown-quality observation to wait and a later proven-quality observation to retry. Native code issues at most one request per playback instance. The collector consumes each monotonically numbered request once.

## Settings and UI

`AutoSkipDownloaded=0` is persisted in `SpotifyHistory.ini`. Missing or invalid values default to off. The menu item is a live check item below Lyrics; changing it does not reset active compressed capture.

## Logging and safety

Normal logging records only that an auto-skip was requested after the verified existing-file and metadata stages, with the song identity. It does not claim the Spotify command succeeded. The collector's ordinary control observation remains the evidence that a Next action occurred and prevents the skipped listen from being treated as naturally completed.

The feature is music-only. It never auto-skips podcasts and never uses Windows media controls.
