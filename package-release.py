#!/usr/bin/env python3
"""Package the automatic DLL and the separate launcher ZIP."""
import argparse
import hashlib
import shutil
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--version', default='1.1.0')
version = parser.parse_args().version
if not version or any(c not in '0123456789abcdefghijklmnopqrstuvwxyz.-' for c in version):
    parser.error('version must contain only lowercase letters, digits, dots and hyphens')
root = Path(__file__).resolve().parent
out = root / 'dist'
out.mkdir(exist_ok=True)
for old_archive in out.glob('Floggfy-v*-Windows-x64.zip'):
    old_archive.unlink()

shutil.copy2(root / 'build' / 'version.dll', out / 'version.dll')
payloads = {
    'Floggfy.exe': (root / 'build' / 'Floggfy.exe').read_bytes(),
    'Floggfy.dll': (root / 'build' / 'version.dll').read_bytes(),
}
payloads['SHA256SUMS.txt'] = ''.join(
    f'{hashlib.sha256(data).hexdigest()}  {name}\n'
    for name, data in payloads.items()).encode()
for name in ['README.md', 'LICENSE', 'SpotifyHistory.ini']:
    payloads[name] = (root / name).read_bytes()
for vendor in ['minhook', 'libogg', 'cef', 'soggfy']:
    payloads[f'third-party/{vendor}/LICENSE.txt'] = (
        root / 'native' / 'vendor' / vendor / 'LICENSE.txt').read_bytes()
path = out / f'Floggfy-v{version}-Launcher-Windows-x64.zip'
with zipfile.ZipFile(path, 'w') as archive:
    for name, data in payloads.items():
        info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o100644 << 16
        archive.writestr(info, data)
print('Automatic: version.dll')
print(f'Launcher: {path.name}')
