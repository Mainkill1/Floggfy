# Manual Duplicate Routing Design

## Outcome

Keep the first verified recording at its canonical library path. If a later
recording has a different complete Spotify track URI but resolves to the same
canonical path, preserve the first file and route the later recording beneath
`Duplicates`, retaining the normal folder hierarchy for manual review.

## Invariants

- Canonical resolution runs first and existing working behavior is unchanged.
- Duplicate routing requires two different, valid, complete Spotify track URIs.
- Missing or legacy identity never authorizes duplicate routing.
- Exact decisions compare the complete URI; filename suffixes are display aids.
- Duplicate names append `[t-<id>]`, initially using the last 10 track-ID
  characters. If that visible suffix is occupied by another complete URI, use
  the last 14 characters, then the complete 22-character ID.
- The filename stem is at most 100 UTF-16 code units. Only the visible title is
  shortened; the selected identity suffix and optional track-number prefix are
  retained.
- Repeat playback rediscovers and reuses the exact canonical or duplicate file.
- Different-track recordings are never merged, repaired from one another, or
  used as upgrade sources.
- Saved metadata retains the complete `SPOTIFY_URI` and, when known, the
  complete `SPOTIFY_ALBUM_URI`.
- A later collision never moves the first canonical file.

## Layout

For a default Music root:

```text
Artists\Artist\Album\Song.flac
Duplicates\Artists\Artist\Album\Song [t-XoxDHR5mz].flac
```

For a custom library root, the duplicate path retains the leading media kind:

```text
Music\Artists\Artist\Album\Song.flac
Duplicates\Music\Artists\Artist\Album\Song [t-XoxDHR5mz].flac
```

Album URI is provenance, not a folder name and not an authorization to replace
audio. This keeps related language/release variants together for manual review
without relying on translated album text as identity.
