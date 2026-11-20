# Recording-owned metadata implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Keep changes in this existing worktree.

**Goal:** Keep complete available metadata with each recording, collect podcast name/date/artwork, and request online enrichment only after local collection cannot supply required fields.

**Architecture:** Reuse the player observer and URI identity. The matcher/save job holds a reference to the metadata record; background enrichment updates that record without making the audio path perform I/O. Gather present fields first, then read local storage for missing tags, then authorize only necessary online service requests. Artwork follows its own validated reference attached to that record.

**Tech Stack:** C++17/Win32, existing CEF observer, JavaScript collector, Node and Windows regression tests.

**Spec:** User-approved conversation: shared metadata record, common song/podcast collection, metadata retained until no longer needed, local-first enrichment, no two-second storage cutoff, MP3 DATE preferred over YEAR.

## Constraints and review focus
- Preserve existing old/new Spotify discovery methods and audio matching semantics.
- No installation/restart or release during implementation; prepare a tested candidate.
- Optional fields may be missing; do not invent dates, artists or image bytes.
- Repeated URI playback and delayed responses must not attach data to another identity.
- Missing later fields must not erase known metadata/artwork.
- Podcasts without music artist/album fields must still retain available title/date/image.
- Metadata/artwork work must not block decoder callbacks or playback observation.
- Keep Metadata/Lyrics switches and existing saved-file opt-outs effective.

## Tasks
- [x] Collector: shared music/podcast snapshot extraction, native and HTTPS artwork references and variants, optional field tolerance. Add focused offline snapshot regressions. Online service requests require explicit native authorization after local collection.
- [x] Record lifetime: matcher/save jobs retain metadata records; missing-field merge preserves artwork; enrichment results update the owning record and remain discoverable for existing-file updates.
- [x] Local enrichment: remove timed memo cache and two-second deadline; use the configured read budget and log failure reasons; avoid repeated work for an unchanged record and fetch online only for remaining meaningful gaps.
- [x] Artwork: investigate a local cache/loaded-image retrieval path using the exact artwork reference; implement a verified bounded route without new offset-based hooks. Keep image absence nonfatal.
- [x] MP3: one TDRC frame, DATE first and YEAR fallback; preserve unrelated tags and audio.
- [x] Verify relevant collector, metadata, artwork, publication and Windows matcher regressions; build DLL; review changes and report limitations honestly.

## Validation and implementation notes
- Portable native suite and collector suites passed, including 10,000 playback lifecycle fuzz cases.
- Windows publication/artwork, album-artist, metadata liveness, cache-reader, and matcher checks passed. Release DLL built successfully; only existing vendor warnings remain.
- Exact cached JPEG and WebP artwork were read offline from the installed Spotify cache. WebP converts to PNG through WIC behind the platform-neutral ReadCachedArtwork interface.
- Unknown browser cache layouts or absent system WebP decoders report a local miss; they do not block saving. Current adapter supports observed Chromium blockfile v3 and the Spotify partition key.
- Native metadata records are pinned through playback/matching/save and pending enrichment; obsolete queued records are pruned. A bounded unowned handoff window remains for renderer messages preceding playback snapshots; the separate timed database-result memo is removed.
- This change does not add automatic revisiting of every previously saved/skipped recording or a new MP3 retagging backend. Existing update policy remains in effect.
- Windows file access, WIC and WinHTTP remain behind existing platform interfaces. This is preparation for portability, not a completed cross-platform port.
- No Spotify restart, installation, firewall change, push or release during this implementation.
