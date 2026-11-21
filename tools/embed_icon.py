"""Wrap the supplied 256px PNG in an ICO container for Windows resources."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
png = (root / "Floggfy - icon.png").read_bytes()
if png[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", png[16:24]) != (256, 256):
    raise ValueError("Launcher icon must be a 256 by 256 PNG")
output = root / "build" / "Floggfy.ico"
output.parent.mkdir(exist_ok=True)
# Zero width/height denotes 256 pixels. Windows accepts PNG data directly.
output.write_bytes(struct.pack("<HHHBBBBHHII", 0, 1, 1, 0, 0, 0, 0, 1, 32, len(png), 22) + png)
