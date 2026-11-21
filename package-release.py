#!/usr/bin/env python3
"""Package the automatic DLL and the separate launcher ZIP."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tempfile
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--version', default=Path(__file__).with_name('VERSION').read_text(encoding='ascii').strip())
version = parser.parse_args().version
if not version or any(c not in '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-' for c in version):
    parser.error('version must contain only ASCII letters, digits, dots and hyphens')
root = Path(__file__).resolve().parent
out = root / 'dist'
out.mkdir(exist_ok=True)
payloads = {
    'Floggfy.exe': (root / 'build' / 'Floggfy.exe').read_bytes(),
    'Floggfy.dll': (root / 'build' / 'version.dll').read_bytes(),
}
payloads['SHA256SUMS.txt'] = ''.join(
    f'{hashlib.sha256(data).hexdigest()}  {name}\n'
    for name, data in payloads.items()).encode()
for name in ['README.md', 'AUDIO-HISTORY.md', 'LICENSE', 'SpotifyHistory.ini']:
    payloads[name] = (root / name).read_bytes()
for vendor in ['minhook', 'libogg', 'cef', 'soggfy', 'stb']:
    payloads[f'third-party/{vendor}/LICENSE.txt'] = (
        root / 'native' / 'vendor' / vendor / 'LICENSE.txt').read_bytes()
payloads['third-party/minimp4/LICENSE'] = (
    root / 'native' / 'vendor' / 'minimp4' / 'LICENSE').read_bytes()
for vendor, names in {
    'libwebp': ['COPYING', 'PATENTS', 'AUTHORS', 'README.floggfy.md'],
    'sqlite': ['README.md', 'sqlite3.h'],
    'stb_image_write': ['README.floggfy.md', 'stb_image_write.h'],
}.items():
    for name in names:
        payloads[f'third-party/{vendor}/{name}'] = (
            root / 'native' / 'vendor' / vendor / name).read_bytes()
# Keep the inflate decoder's license with the binary distribution.
for name in ['puff.h', 'UPSTREAM.md']:
    payloads[f'third-party/puff/{name}'] = (
        root / 'native' / 'vendor' / 'puff' / name).read_bytes()
for name in ['telemetry-testing.md', 'telemetry-schema-source.md', 'telemetry-schema-1.3.9.2055.json']:
    payloads[f'docs/{name}'] = (root / 'docs' / name).read_bytes()
path = out / f'Floggfy-v{version}-Launcher-Windows-x64.zip'
# Complete both outputs before replacing or removing any published files.
stage = Path(tempfile.mkdtemp(dir=out, prefix='.package-release-'))
keep_stage = False
try:
    dll = stage / 'version.dll'
    dll.write_bytes(payloads['Floggfy.dll'])
    archive_path = stage / path.name
    with zipfile.ZipFile(archive_path, 'w') as archive:
        for name, data in payloads.items():
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    published_dll = out / dll.name
    previous_dll = stage / 'previous-version.dll'
    try:
        shutil.copy2(published_dll, previous_dll)
    except FileNotFoundError:
        previous_dll = None

    dll.replace(published_dll)
    try:
        archive_path.replace(path)
    except OSError:
        try:
            if previous_dll is None:
                published_dll.unlink()
            else:
                previous_dll.replace(published_dll)
        except OSError as error:
            keep_stage = True
            raise RuntimeError(
                f'Could not restore {published_dll}. Staging retained at {stage}'
            ) from error
        raise
finally:
    if not keep_stage:
        shutil.rmtree(stage)
for old_archive in out.glob('Floggfy-v*-Windows-x64.zip'):
    if old_archive != path:
        old_archive.unlink()
print('Automatic: version.dll')
print(f'Launcher: {path.name}')
