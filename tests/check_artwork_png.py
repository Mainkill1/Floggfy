# Decode a generated PNG independently with Python's zlib and check RGBA pixels.
import struct, sys, zlib
b = open(sys.argv[1], 'rb').read()
assert b[:8] == b'\x89PNG\r\n\x1a\n'
pos, compressed = 8, b''
while pos < len(b):
    size = struct.unpack_from('>I', b, pos)[0]
    kind, data = b[pos+4:pos+8], b[pos+8:pos+8+size]
    if kind == b'IHDR': assert data == struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)
    if kind == b'IDAT': compressed += data
    pos += size+12
raw = zlib.decompress(compressed)
assert len(raw) == 18
pixels, previous = [], [0]*8
for start in (0, 9):
    kind, row = raw[start], list(raw[start+1:start+9])
    for x in range(8):
        a, b, c = row[x-4] if x >= 4 else 0, previous[x], previous[x-4] if x >= 4 else 0
        if kind == 1: row[x] = (row[x]+a)&255
        elif kind == 2: row[x] = (row[x]+b)&255
        elif kind == 3: row[x] = (row[x]+((a+b)//2))&255
        elif kind == 4:
            p = a+b-c; distances = [abs(p-a), abs(p-b), abs(p-c)]
            row[x] = (row[x]+[a,b,c][distances.index(min(distances))])&255
        else: assert kind == 0
    pixels += row; previous = row
assert pixels[:12] == [255,0,0,255,0,255,0,128,0,0,255,64]
assert pixels[15] == 0  # Transparent RGB may be discarded by the fixture encoder.
print('PASS: independent PNG decode preserves RGB and alpha values')
