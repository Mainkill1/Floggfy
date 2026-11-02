# Podcast Capture Design

## Intent

Add an explicit, default-off **Podcasts** option to To Disk. When enabled, a
fully listened Spotify episode is saved under `Podcasts\<Show>\<Episode>.mp3`.
Music capture and its folder layout remain unchanged.

## Constraints

- Preserve Spotify's original compressed bytes; never record PCM or transcode.
- Accept only `spotify:episode:` playback for the podcast path.
- Fail closed on unsupported framing, malformed/truncated MP3 frames, missing
  playback identity, skips/seeks, queue loss, overlapping decoders, or an
  unverified capture/listen association.
- Keep Downloads as the master switch. Podcasts defaults to disabled and can be
  changed live from the To Disk menu.
- A complete episode must receive an owned metadata tag so duplicate and
  collision handling can use the same staged publication rules as music.

## Evidence and approach

Two unrelated live episodes were observed on Spotify 1.3.6.1921. Both use the
existing decoded dispatcher and expose the original consumed MP3 span: one
began with ID3v2.3 and one with ID3v2.4. A 119-second episode completed in about
11 seconds at 10x, proving the dispatcher is shared even though the existing
Ogg/FLAC/SpAC hooks receive no podcast bytes.

The selected approach copies only decoder contexts whose first consumed span is
an ID3/MP3 start. The worker validates contiguous MPEG Layer III frame
boundaries, sample rate, channels, and duration, then binds the decoder context
to the episode playback instance through the existing ownership tracker. The
published file gets a bounded ID3v2.4 tag built from the episode metadata while
all MP3 audio frames remain byte-for-byte unchanged.

Alternatives rejected:

- PCM/WAV capture: large and transcoded, and discards the source MP3.
- A menu-only episode gate: live testing proved the existing compressed hooks
  see no podcast bytes.
- Fetching podcast URLs: adds a separate authenticated network/download path and
  loses the decoder ownership evidence already available locally.

## Metadata and layout

`spotify:episode:` selects `MediaKind::Podcast`. The show name is the playback
album/show field and the publisher remains the artist tag. Podcast files never
enter `Music\Artists`. Episode footer metadata is emitted without track/album
service or lyrics requests.

