"""A failed package build must preserve the previous release files."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    source = Path(__file__).resolve().parents[1] / 'package-release.py'
    shutil.copyfile(source, root / source.name)
    (root / 'build').mkdir()
    (root / 'dist').mkdir()
    (root / 'build' / 'version.dll').write_bytes(b'new build')
    old_files = {
        'Floggfy-v1.3-Launcher-Windows-x64.zip': b'previous launcher archive',
        'version.dll': b'previous packaged dll',
    }
    for name, contents in old_files.items():
        (root / 'dist' / name).write_bytes(contents)

    result = subprocess.run([sys.executable, str(root / source.name)], capture_output=True)
    assert result.returncode != 0 and b'Floggfy.exe' in result.stderr
    assert {path.name: path.read_bytes() for path in (root / 'dist').iterdir()} == old_files

print('PASS: failed packaging preserves the previous launcher archive and DLL')
