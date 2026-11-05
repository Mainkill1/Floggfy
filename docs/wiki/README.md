# Floggfy implementation wiki

This wiki is a high-level map of how Floggfy records audio already being played by
the Windows Spotify desktop client, proves which playback it belongs to, enriches
it, and publishes it without silently replacing unrelated files.

```mermaid
flowchart LR
    S[Spotify desktop client] --> H[Audio hooks]
    S --> C[Client-state collector]
    S --> W[Windows media fallback<br/>explicit opt-in]
    H --> Q[Bounded capture queues]
    H --> D[Decoded-output observations]
    C --> L[Playback and metadata observations]
    W --> L
    Q --> A[Capture correlation]
    D --> A
    L --> A
    A --> V[Complete listen + complete recording]
    V --> M[Metadata/tag construction]
    M --> P[Validated staged publication]
    P --> F[Music or Podcasts library]
```

The major rule is simple: compressed bytes are the recording; decoded-output and
playback observations are evidence about ownership. Floggfy does not write decoded
PCM to disk and does not independently fetch audio.

## Read next

- [Capture and correlation](capture-and-correlation.md) — hook points, codec paths,
  listen coverage, decoder ownership, and capture speed.
- [Metadata and publication](metadata-and-publication.md) — enrichment, tag merge,
  library layout, duplicate handling, repair, and atomic publication.
- [Operations and safety](operations-and-safety.md) — settings, optional
  capabilities, bounded work, failures, logs, and maintenance expectations.
- [FLAC overlap and repair incident](flac-overlap-and-repair-incident.md) — a
  concrete duplicate-decoder incident, the evidence it produced, and safe ideas
  for classifying or repairing overlapping compressed captures.

For exact policy constraints, see the maintained
[capture and publication invariants](../capture-invariants.md). User-facing setup
and settings remain in the project [README](../../README.md).

## Component map

| Responsibility | Primary implementation |
| --- | --- |
| Hook discovery and structural validation | [`spotify_hook_discovery.cpp`](../../native/spotify_hook_discovery.cpp) |
| Audio hooks, queues, association, and publication scheduling | [`audio_history.cpp`](../../native/audio_history.cpp) |
| Decoder/playback ownership stages | [`decoder_ownership.cpp`](../../native/decoder_ownership.cpp) |
| Spotify client timeline and enrichment collector | [`metadata_collector.js`](../../native/metadata_collector.js) |
| Collector injection and native message bridge | [`metadata_bridge.cpp`](../../native/metadata_bridge.cpp) |
| Optional Windows media-session fallback | [`media_session.cpp`](../../native/media_session.cpp) |
| Recording inspection and destination resolution | [`recording_file.cpp`](../../native/recording_file.cpp), [`existing_quality.cpp`](../../native/existing_quality.cpp) |
| Metadata merge and codec-specific tagging | [`metadata_update_core.cpp`](../../native/metadata_update_core.cpp) |
| Staged, no-overwrite file publication | [`file_publication.cpp`](../../native/file_publication.cpp) |
| Folder and filename layout | [`library_layout.cpp`](../../native/library_layout.cpp) |

