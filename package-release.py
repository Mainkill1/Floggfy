#!/usr/bin/env python3
"""Package the release DLL and public documentation."""
import hashlib
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parent
out = root / 'dist'
out.mkdir(exist_ok=True)
release_name = 'Floggfy-v1.1.0-Windows-x64.zip'
for old_archive in out.glob('Floggfy-v*-Windows-x64.zip'):
    if old_archive.name != release_name:
        old_archive.unlink()
shutil.copy2(root / 'build' / 'version.dll', out / 'version.dll')
digest = hashlib.sha256((out / 'version.dll').read_bytes()).hexdigest()
(out / 'SHA256SUMS.txt').write_text(f'{digest}  version.dll\n')
with zipfile.ZipFile(out / release_name, 'w') as archive:
    def add(path, arcname):
        info = zipfile.ZipInfo(arcname, (1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o100644 << 16
        archive.writestr(info, Path(path).read_bytes())

    for name in ['version.dll', 'SHA256SUMS.txt']:
        add(out / name, name)
    for name in ['README.md', 'LICENSE', 'SpotifyHistory.ini']:
        add(root / name, name)
    for vendor in ['minhook', 'libogg', 'cef', 'soggfy']:
        add(root / 'native' / 'vendor' / vendor / 'LICENSE.txt',
            f'third-party/{vendor}/LICENSE.txt')
print(f'DLL: {out / "version.dll"}')
print(f'SHA256: {digest}')
print(f'ZIP: {out / release_name}')
