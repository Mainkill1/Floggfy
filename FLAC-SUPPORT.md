# Native FLAC capture

## Observed format and implemented boundary

Spotify 1.3.1.234 x64 Lossless playback reports format `4` with the native `fLaC`
header. Its Ogg parser receives zero calls. This format mismatch explained why
Ogg-only capture could not save these tracks; it is not evidence of hook detection.

The current implementation hooks the pinned native FLAC wrapper:

| Spotify.dll RVA | Function | Verified arguments |
| --- | --- | --- |
| `0xE95F2C` | Decoder initialization | context, result |
| `0xE9663C` | Compressed-input reader | context, bytes, size_t* |
| `0xE969F4` | Decoded-frame callback | context, FLAC frame, PCM channel pointers |
| `0xE959D0` | Decoder-error callback | context, error code |

Initialization resets the stream identity. The reader's returned compressed bytes
are copied into a bounded queue, then accumulated in RAM. The frame callback
checks sequential sample/frame numbers, block sizes, rate, channel count and bit
depth against STREAMINFO. Any decoder error or coverage gap invalidates the stream.
Publication requires all STREAMINFO samples plus a complete audible listen. The
existing native FLAC decoder continues handling playback unchanged.

Hook targets are discovered in the loaded Spotify DLL from decoder instruction
anchors and Windows x64 function metadata. Missing, ambiguous or inconsistent
matches fail closed before any audio hook is installed. Signed Spotify files are
never modified.

## Single-file native tagging

`native/flac_history_core.cpp` preserves STREAMINFO, source audio MD5 and compressed
frame bytes. It replaces Vorbis comments and front-cover artwork with native
VORBIS_COMMENT and PICTURE blocks, removes unused padding, and preserves other
metadata blocks. Seek-table offsets remain relative to the first audio frame.
There are no `.part` files, image/JSON sidecars, encoders, transcoders or FFmpeg.

The format and checksum reference is [RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html).
The decoder ABI reference is the [Xiph FLAC source](https://github.com/xiph/flac/blob/1.4.3/include/FLAC/stream_decoder.h).

## Verification

Portable tests cover metadata boundaries, malformed STREAMINFO, fixed and variable
frame numbering, sample gaps, retagging, embedded artwork and exact compressed-frame
preservation. A real generated FLAC fixture was tagged by the native implementation
and independently fully decoded with the Xiph FLAC CLI; its original STREAMINFO
MD5 still matched. The CLI is used for development verification only.

`tools/validate_flac.py` independently checks the stored metadata, embedded
picture, full decoding, sample count, frame CRCs and source MD5 when supplied.

## Remaining limits

FLAC streams without a known total sample count are rejected. General podcast/
audiobook category enrichment and video extraction remain separate work. The
memory cap can reject long recordings; it never spills partial tracks to disk.
Future Spotify builds with rewritten decoder boundaries need fresh ABI and live
capture validation before adding new instruction anchors. Additional live tests
for higher bit depths, same-title repeats, long pauses, seeks, mixed-codec
transitions and overflow remain useful coverage.
