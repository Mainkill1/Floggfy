# Native-Format Podcasts

## Intent

Stop inferring media kind from its audio codec. A Spotify episode is a podcast
because its metadata identifies it as an episode, whether Spotify delivers MP3,
Ogg Vorbis, AAC, or FLAC.

## Goals

- Keep capture and container validation codec-specific but media-neutral.
- Reconcile a complete capture with observed playback using ownership, playback
  instance, timing, and duration without inferring media type from codec.
- Classify the reconciled item from metadata only after association, then apply
  media policy and destination planning during publication.
- Bind complete episode captures to episode metadata independently of codec.
- Save the original compressed stream without transcoding, using `.mp3`, `.ogg`,
  `.m4a`, or `.flac` as appropriate.
- Route every episode to `Podcasts\<show>\...` through the existing podcast
  catalog layout.
- Require `Podcasts=1` for every episode. Also require the matching source-format
  switch for Ogg, FLAC, and AAC. MP3 capture follows the master `Downloads`
  switch because media kind is not known in the decoder hook; episode publication
  still requires `Podcasts=1`.
- Preserve current music behavior and the same capture-integrity, complete-listen,
  ownership, metadata, and publication checks.

## Pipeline

1. Decoder hooks capture enabled source formats and validate their native
   containers without consulting track/episode metadata. MP3 input is captured
   whenever downloads are enabled, then classified later like every other codec.
2. Playback observation records a generic completed listen.
3. Reconciliation associates capture and listen from exact decoder/playback
   ownership where available, with the existing fail-closed timing and duration
   safeguards.
4. Metadata classifies the associated listen as a podcast or another catalog
   kind.
5. Publication applies the independent codec and media settings, then tags and
   routes the original stream.

Codec-specific parsing remains necessary; codec-specific guesses about media
kind do not. Advertisement/interstitial exclusion and ambiguity rejection remain
unchanged.

## Non-goals

- No MP3 conversion or other transcoding.
- No weakening of association checks to recover ambiguous captures.
- No automatic publication of old dump files; they remain forensic test input.
- No INI option changes in this fix.
- No broad worker rewrite or unrelated cleanup before the behavioral fix is
  verified.

## Approaches

1. **Selected: native-format, codec-independent episodes.** Media metadata decides
   podcast versus music; codec decides the extension and codec-format gate. This
   matches Floggfy's original-stream design and preserves quality.
2. **Rejected: transcode non-MP3 episodes to MP3.** This would add an encoder,
   consume CPU, alter the source audio, and contradict the documented no-conversion
   behavior.

## Verification

- Add a regression that fails under the current `podcast == MP3` assumption.
- Cover Ogg and FLAC episode association and format gating, plus existing MP3
  podcast and music behavior.
- Replay or independently inspect the captured long Ogg materials to confirm
  continuous pages, valid CRCs/EOS, and duration agreement.
- Run the focused Windows tests and the complete repository test suite before
  installing another build or requesting a live playback test.
- After verification, review the affected pipeline and surrounding worker for
  redundant media checks, duplicated policy, and unnecessary background work.
  Keep review findings separate from the fix unless a trim is mechanically safe
  and covered by the same tests.
