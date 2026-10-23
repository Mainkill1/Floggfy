"""Small complete, checksummed, tagged containers for publication tests."""
import struct
import sys
from pathlib import Path
root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)

def comments(codec):
    fields = [b'TITLE=Track', b'ARTIST=Artist', b'ALBUM=Album',
              b'SPOTIFY_URI=spotify:track:test', b'HISTORY_COMPLETE_LISTEN=1',
              b'HISTORY_CAPTURE=native compressed ' + codec]
    return struct.pack('<II', 0, len(fields)) + b''.join(struct.pack('<I', len(f)) + f for f in fields)

def page(packet, sequence, flags=0, granule=0):
    laces = bytes([255] * (len(packet) // 255) + [len(packet) % 255])
    result = bytearray(b'OggS' + bytes([0, flags]) + struct.pack('<QIII', granule, 123, sequence, 0)
                       + bytes([len(laces)]) + laces + packet)
    crc = 0
    for value in result:
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
    result[22:26] = struct.pack('<I', crc)
    return result

def ogg(bitrate):
    identification = b'\x01vorbis' + struct.pack('<IBIiiiBB', 0, 2, 44100, 0, bitrate, 0, 0xb8, 1)
    return (page(identification, 0, 2) + page(b'\x03vorbis' + comments(b'Ogg pages') + b'\x01', 1)
            + page(b'\x05vorbis', 2) + page(b'\x00\x01\x02\x03', 3, 4, 44100))

(root / 'low.ogg').write_bytes(ogg(160000))
(root / 'high.ogg').write_bytes(ogg(320000))
info = bytearray(34)
info[:4] = struct.pack('>HH', 16, 16)
info[10:18] = ((44100 << 44) | (1 << 41) | (15 << 36) | 16).to_bytes(8, 'big')

def crc(bytes_, width, polynomial):
    result = 0
    for byte in bytes_:
        result ^= byte << (width - 8)
        for _ in range(8):
            result = ((result << 1) ^ (polynomial if result & (1 << (width - 1)) else 0)) & ((1 << width) - 1)
    return result

frame = bytearray([0xff, 0xf8, 0x60, 0x10, 0, 15])
frame += bytes([crc(frame, 8, 7)])
frame += bytes(6)  # two constant 16-bit subframes
frame += crc(frame, 16, 0x8005).to_bytes(2, 'big')
tags = comments(b'FLAC input')
(root / 'existing.flac').write_bytes(b'fLaC\x00\x00\x00\x22' + info + b'\x84' + len(tags).to_bytes(3, 'big') + tags + frame)
(root / 'unknown.ogg').write_bytes(b'unrecognized quality fixture')
(root / 'header-only.flac').write_bytes(b'fLaC\x80\x00\x00\x22' + info)
