# Spotify 1.3.10 playback identity compatibility

**Goal:** Restore exact audio/metadata matching on Spotify 1.3.10.2586, based on 1.4-RC2, without changing timing tolerances or accepting uncertain matches.

**Evidence:** Stable 1.3 receives metadata and complete FLAC audio, but every native identity query misses. Read-only inspection finds the current metadata playback ID at player-state offset 0xA08. The existing window covers 0x9B8–0x9D7; the previously validated payload was at 0x9C0. The installed-hook gate consequently leaves complete recordings waiting for identity.

**Design:** Discover the field from a unique full metadata playback ID within a bounded readable player-state prefix (maximum 4096 bytes). Retain at most 16 startup discovery snapshots, preserving original callback times for metadata that arrives after a decoder ends. Keep the first and latest retry evidence; throttle same-owner retries to 250 ms. After calibration, retain only normal 32-byte identity observations. Publish the learned absolute field offset atomically so an MP3 boundary never depends on obtaining the metadata-worker lock. Revalidate associations through the existing exact-ID matcher; a preload differing from current UI metadata does not invalidate the learned process layout.

**Fallback correction:** An attached hook alone must not veto unique media/metadata evidence. For the newly enabled fallback path, require complete validated media, timing and duration agreement, uniqueness in both collections, no observed native identity conflict, no lost identity evidence, and existing retirement/lifecycle safeguards. Keep fallback associations explicitly weaker than exact native matches.

**Validation and implementation:**

- [x] Reproduce the displaced 0xA08 payload failure in the identity-reader/bridge test.
- [x] Implement bounded runtime field discovery and update mock player allocations.
- [x] Verify old/new layouts, delayed metadata, duplicate-ID rejection, unreadable memory, and captured live field evidence.
- [x] Verify deferred initialization, readable-page boundaries, and lock-independent MP3 boundaries.
- [x] Reproduce and fix the completed-buffer fallback veto; reject conflicts, partial media and ambiguous alternatives.
- [x] Run relevant Windows recording/metadata diagnostics and build the 1.4-based DLL.
- [x] Review changes and report the artifact and remaining live validation. Do not install or publish during the investigation.

Offline metadata comes from the local PlayerAPI snapshot before optional network enrichment. Existing logs prove that basic metadata reaches the matcher in this session; online/offline mode itself is not recorded, so do not claim a controlled mode comparison.

The existing upstream source/state pointer path remains a compatibility boundary; this change discovers the playback-ID field, not arbitrary future Spotify object graphs. Unsupported discovery must be reported and must not silently turn into exact identity proof.
