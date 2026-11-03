# Capture and correlation

## What is hooked

Floggfy discovers private Spotify functions from the loaded x64 image. Discovery
does not trust one byte signature alone: it checks executable sections, x64 unwind
function boundaries, uniqueness, and expected relationships between targets. If a
required target is missing or ambiguous, the audio hook transaction is not
activated. See [`spotify_hook_discovery.cpp`](../../native/spotify_hook_discovery.cpp).

| Path | Hooked stage | What Floggfy retains |
| --- | --- | --- |
| Ogg Vorbis | Ogg page-seek/parser return | The complete returned Ogg page, including its original header and body |
| FLAC | Decoder initialize, compressed read, frame callback, and error callback | Original compressed FLAC bytes plus frame/sample coverage used to prove completeness |
| AAC | Optional SPAC reader initialize, compressed packet input, and destroy | Original SPAC/AAC packets, later remuxed into M4A without transcoding |
| Decoded output | Optional shared decoder dispatcher | Counts, span movement, decoder identity, and producer order; never saved PCM |
| Podcast MP3 | Encoded input exposed by the decoded dispatcher | Original MPEG audio bytes for enabled podcast episodes |

The callbacks do minimal work: call Spotify as required, validate bounds, copy a
bounded observation, assign a shared producer sequence, and wake the worker. Codec
parsing, metadata work, and filesystem access stay off Spotify's callback threads.
The hook implementations are grouped in
[`audio_history.cpp`](../../native/audio_history.cpp).

## Codec lifecycles

- **Ogg** is tracked per decoder context and logical-stream serial number. Pages
  are checked for capture pattern, sequence, checksum, granule progression, and
  end-of-stream before the stream becomes complete.
- **FLAC** starts at decoder initialization. STREAMINFO and original input are
  retained, while frame callbacks account for covered samples. A clean decoder end
  and complete declared sample coverage are both required.
- **AAC** is optional. SPAC input is parsed as bounded packets and completeness is
  checked before an M4A container is built.
- **MP3** is reserved for podcast capture. MPEG frames and owned tags are inspected;
  the published sample count becomes later evidence that a file was not truncated.

Each compressed and decoded observation shares one monotonic producer sequence.
This preserves causality when the worker receives a compressed completion before
the outer decoded callback, and prevents two lifetimes that reuse the same address
from being merged.

## Ownership stages

The ownership tracker correlates evidence; it does not decide from a filename or
from whichever stream finishes first.

1. **Capture began.** A codec lifetime is assigned a capture token and normalized
   decoder owner.
2. **Decoded output observed.** Output from that owner proves that the decoder was
   active for the capture lifetime.
3. **Playback stage corroborated.** A Ready listen generation, media kind, and
   codec-declared duration select exactly one decoded capture. With the explicit
   Windows fallback enabled, the URI may come from the app transition while
   playback state and duration come from Windows.
   After binding, that URI remains the listen identity while Windows refines its
   title or artist; it is not carried across a successor transition.
4. **Capture completed.** Codec-specific structural evidence proves that the
   compressed stream is whole.
5. **Candidate selected.** A completed listen must have one compatible, complete
   recording. Competing active or completed candidates keep the result ambiguous.

Spotify may preload one successor decoder before the outgoing decoder completes.
The tracker therefore keeps a current capture and one pending successor. A
transition registers the possible successor but does not bind it. The following
Ready stage must corroborate media kind and duration before `audio_history.cpp`
explicitly selects the capture. Multiple candidates, queue loss, or incompatible
media kind prevent a guess. See
[`decoder_ownership.cpp`](../../native/decoder_ownership.cpp).

## Listen coverage

Playback observations come primarily from the injected client collector. It emits
URI, playback instance, position, duration, local-device state, pause/buffering
state, speed, and observed controls. Windows Global System Media Transport Controls
provide a bounded fallback snapshot only when `Windows Playback Fallback=1`; the
option defaults to `0`, so unavailable Spotify state otherwise remains unavailable.

[`ogg_history_core.cpp`](../../native/ogg_history_core.cpp) maintains a listen
generation and media-position coverage. Starting mid-track, seeking, skipping,
remote playback, unknown device ownership, or missing observations cannot become a
complete listen. Pauses stop progress but do not discard already established
coverage. Natural transitions and same-song repeats are separate lifecycle events.

Association occurs only after both sides agree:

- the listen is complete and eligible;
- the recording format is enabled for the media kind;
- media kind and codec-declared duration agree;
- ownership is bound where decoded observation is available;
- no competing capture can claim the same listen; and
- final whole-buffer validation succeeds.

## Capture speed

Capture speed uses the optional decoded-output hook. Spotify still consumes the
same compressed input; Floggfy advances the decoded output span so fewer PCM blocks
reach playback. The cached Ogg, FLAC, AAC, or MP3 bytes are not resampled or
transcoded. If the dispatcher ABI or span movement fails validation, acceleration
is disabled and capture falls back to 1x. The filtering logic is in
[`decoded_output_speed.h`](../../native/decoded_output_speed.h).

Acceleration also requires an explicit startup ceiling in `SpotifyHistory.ini`.
`CaptureSpeedLimit` is off when missing or set to `0`/`1`, accepts ceilings from
`2` through `10`, and clamps larger values to `10`. The menu enables only supported
speeds at or below that ceiling after the decoded-output hook is active; 1x remains
available in every capability state.

