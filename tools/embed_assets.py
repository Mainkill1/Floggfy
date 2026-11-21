"""Embed the renderer scripts and the shipped first-run INI in the Windows DLL."""
import json
from pathlib import Path
from tempfile import NamedTemporaryFile

root = Path(__file__).resolve().parents[1]
build = root / "build"
build.mkdir(exist_ok=True)


def write_header(symbol, declaration):
    # Standalone test runners can generate the same headers concurrently.
    with NamedTemporaryFile(dir=build, mode="w", encoding="utf-8", newline="\n", delete=False) as out:
        out.write(declaration)
    Path(out.name).replace(build / f"{symbol}.h")


def embed_script(symbol, *sources):
    script = "\n".join((root / "native" / name).read_text(encoding="utf-8") for name in sources)
    write_header(symbol, f'static constexpr wchar_t {symbol}[]=LR"FLOGGFY({script})FLOGGFY";\n')


embed_script("metadata_script", "marked_playlist_sort.js", "marked_playlist.js", "artwork_collector.js", "metadata_collector.js")
embed_script("telemetry_script", "telemetry_producers.js")
ini = (root / "SpotifyHistory.ini").read_text(encoding="ascii").replace("\n", "\r\n")
write_header("default_ini", f"static constexpr char default_ini[]={json.dumps(ini)};\n")
