stb_vorbis v1.22 from nothings/stb, file revision
`1ee679ca2ef753a528db5ba6801e1067b40481b8`:
https://github.com/nothings/stb/blob/1ee679ca2ef753a528db5ba6801e1067b40481b8/stb_vorbis.c

Original source SHA256: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`.
The complete upstream license is retained in the source and LICENSE.txt.

Floggfy uses only pushdata opening/header inspection, with no audio-frame
decoding or encoding. The adapter supplies a fixed 8 MiB allocation arena,
limits the setup packet to 1 MiB and channels to eight, and substitutes a
small canonical comment packet after the application's existing comment
validation. The upstream shared setup CRC-table initialization is serialized.

Local hardening changes, marked `Floggfy` in the source:

- Reject negative/overflowing allocation sizes before alignment and use
  subtraction for the arena capacity comparison; report final decoder-object
  allocation exhaustion explicitly.
- Reject empty or oversized codebooks (dimensions over 256 or entries over
  65,536) before table allocations, products, shifts, and expansion.
- Use unsigned bit accumulation to avoid signed left-shift overflow.
- Require the setup framing bit and reject truncation or extra packet bytes.
  Unused bits in the last byte remain ignored.
- Check Huffman-tree occupancy, including the one-bit single-entry exception
  from Vorbis I section 3.2.1; upstream otherwise accepts underpopulated trees.

The wrapper disables pull/stdio APIs, integer conversion, and codebook
lookup-table expansion. Unsupported floor 0, channel/codebook limits, setup
size limits, or arena exhaustion are reported as unsupported, never as valid
headers. They cannot authorize duplicate suppression or replacement. A valid
header result is not a full audio decode or proof that every audio packet is
decodable; Ogg CRC, sequence, packet, and EOS checks remain separate.

Compile `native/vorbis_headers_stb.c` as a separate C11 vendor object with
`-O2 -Wall -Wextra`, matching the existing vendor-object build policy. The
C++ adapter includes declarations only and retains project `-Werror`; no
diagnostic pragmas or warning-suppression options are used.
