#!/usr/bin/env python3
"""Package the release DLL and public documentation."""
import hashlib
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parent
out = root / 'dist'
out.mkdir(exist_ok=True)
shutil.copy2(root / 'build' / 'version.dll', out / 'version.dll')
digest = hashlib.sha256((out / 'version.dll').read_bytes()).hexdigest()
(out / 'SHA256SUMS.txt').write_text(f'{digest}  version.dll\n')
with zipfile.ZipFile(out / 'Floggfy-v1.0-Windows-x64.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for name in ['version.dll', 'SHA256SUMS.txt']:
        archive.write(out / name, arcname=name)
    for name in ['README.md', 'LICENSE', 'SpotifyHistory.ini']:
        archive.write(root / name, arcname=name)
    for vendor in ['minhook', 'libogg', 'cef', 'soggfy']:
        archive.write(root / 'native' / 'vendor' / vendor / 'LICENSE.txt',
                      arcname=f'third-party/{vendor}/LICENSE.txt')
print(f'DLL: {out / "version.dll"}')
print(f'SHA256: {digest}')
print(f'ZIP: {out / "Floggfy-v1.0-Windows-x64.zip"}')
