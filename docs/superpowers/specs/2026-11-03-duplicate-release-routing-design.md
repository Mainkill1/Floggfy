# Explicit UID Collision Fallback Design

## Intent

Preserve Floggfy's working destination, conversion, matching, upgrade, and metadata behavior. Add one final fail-safe path only when an existing recording and the current Spotify track carry explicit, different `SPOTIFY_URI` values.

## Goals

- Keep the current canonical path and resolver as the primary behavior.
- Detect a proven track-identity collision after ordinary destination resolution.
- Route only the colliding incoming track below the selected root's `Duplicates` directory.
- Reuse the existing collision, quality, metadata, staging, and publication safeguards inside that directory.
- Make later metadata checks, Auto Skip Downloaded, and current-song folder lookup find the same duplicate path.

## Non-goals

- Album ownership, album URI transport, folder markers, folder renames, or a database.
- Changing legacy textual matching or existing URI-less behavior.
- Moving existing files or reorganizing already mixed folders.
- Treating a missing UID as proof of a collision.
- Playlist-based storage or podcast routing changes.

## Trigger

The existing resolver runs first and remains authoritative. A new duplicate fallback is considered only when:

1. no existing candidate matching the current recording was found;
2. a candidate occupying the ordinary canonical namespace was successfully inspected;
3. both the existing file and current catalog contain a Spotify track URI; and
4. those complete URIs differ.

Same URI follows the existing path. Missing/malformed URI follows existing conservative behavior. Text, duration, language, date, filenames, and album name never trigger this path.

## Duplicate path

The fallback preserves the ordinary hierarchy below a root `Duplicates` directory and appends the complete track identifier to the filename:

```text
Duplicates\Artists\Disney\Frozen\Song [spotify-<22-character-track-id>].flac
```

For configurations that retain the `Music` hierarchy, it becomes `Duplicates\Music\Artists\...`. The complete identifier is preserved. The human-readable song title is shortened as needed so the filename component remains within the existing 100 UTF-16-code-unit product limit without splitting a surrogate pair.

## Management

- `ResolveRecordingDestination` reports that it observed an explicit different UID but otherwise keeps its current result and search order.
- Callers enter the duplicate namespace only when no exact existing recording was found and the explicit-conflict report is present.
- The duplicate namespace is resolved with the same production resolver. Repeated playback therefore finds, updates, skips, or upgrades the same exact track.
- Cross-format checks carry the conflict: a different-UID Ogg prevents an incoming FLAC from being published at the canonical same-name path.
- Publication still validates complete audio and exact current recording identity immediately before its staged atomic save.
- No folder or file is renamed as part of this feature.

## Failure handling

- A different UID can only redirect a new recording; it never authorizes overwrite, deletion, metadata edits, or Auto Skip.
- Inspection uncertainty remains blocked under existing behavior and is not converted into a duplicate.
- If the duplicate destination cannot be listed, created, validated, or published, existing files remain untouched and the operation reports failure through the existing capture lifecycle.
- A duplicate path is never selected from a missing UID or textual similarity.

## Validation

- Different explicit track URIs at the same canonical name route to `Duplicates`.
- Same URI, URI-less legacy files, unrelated invalid files, and normal vacancies preserve current results.
- An existing matching duplicate is reused across FLAC, Ogg Vorbis, and AAC/M4A.
- A canonical conflict in one codec redirects publication in another codec.
- Metadata update, Auto Skip, filename conversion, and current-song lookup locate the duplicate recording.
- Long Unicode titles preserve the full track identifier and component limit.
- Locks, read errors, and raced destinations fail closed without changing the existing file.

