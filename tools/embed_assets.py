"""Embed the renderer scripts and the shipped first-run INI in the Windows DLL."""
import json
from pathlib import Path
from tempfile import NamedTemporaryFile

root = Path(__file__).resolve().parents[1]
build = root / "build"
build.mkdir(exist_ok=True)
version = (root / "VERSION").read_text(encoding="ascii").strip()
parts = version.split(".")
if len(parts) != 3 or any(not part.isascii() or not part.isdecimal() or int(part) > 65535 for part in parts):
    raise ValueError("VERSION must contain major.minor.patch")
resource_version = ",".join(parts + ["0"])
(build / "version_info.h").write_text(
    f'#define FLOGGFY_VERSION "{version}"\n'
    f'#define FLOGGFY_VERSION_W L"{version}"\n'
    f'#define FLOGGFY_VERSION_RESOURCE {resource_version}\n', encoding="ascii")


def write_header(symbol, declaration):
    # Standalone test runners can generate the same headers concurrently.
    with NamedTemporaryFile(dir=build, mode="w", encoding="utf-8", newline="\n", delete=False) as out:
        out.write(declaration)
    Path(out.name).replace(build / f"{symbol}.h")


def embed_script(symbol, *sources):
    script = "\n".join((root / "native" / name).read_text(encoding="utf-8") for name in sources)
    write_header(symbol, f'static constexpr char {symbol}[]=u8R"FLOGGFY({script})FLOGGFY";\n')


embed_script("metadata_script", "ui/marked_playlist_sort.js", "ui/marked_playlist.js", "media/artwork_collector.js", "media/metadata_collector.js")
embed_script("telemetry_script", "telemetry/telemetry_producers.js")
ini = (root / "SpotifyHistory.ini").read_text(encoding="ascii").replace("\n", "\r\n")
write_header("default_ini", f"static constexpr char default_ini[]={json.dumps(ini)};\n")
