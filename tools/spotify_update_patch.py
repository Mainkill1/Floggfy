#!/usr/bin/env python3
"""Restore a Spotify DLL modified by an earlier Floggfy test build."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile
from contextlib import contextmanager

UPDATE_URL = b'https://spclient.wg.spotify.com/desktop-update/v2/update'
LOOPBACK_URL = b'https://127.0.0.1/desktop-updates/are/not/desired/thanks'
assert len(UPDATE_URL) == len(LOOPBACK_URL) == 56


@contextmanager
def staged_copy(target, contents):
    """Write and flush beside the destination before publishing."""
    fd, name = tempfile.mkstemp(prefix='.floggfy-update-', dir=target.parent)
    staged = Path(name)
    try:
        with os.fdopen(fd, 'wb') as output:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        yield staged
    finally:
        staged.unlink(missing_ok=True)


def update_offset(contents):
    pe = int.from_bytes(contents[60:64], 'little')
    if (contents[:2] != b'MZ' or pe < 64 or pe + 24 > len(contents)
            or contents[pe:pe + 4] != b'PE\0\0' or not contents[pe + 23] & 0x20):
        raise ValueError('Expected a Windows PE DLL.')

    original = UPDATE_URL + b'\0'
    patched = LOOPBACK_URL + b'\0'
    original_count = contents.count(original)
    patched_count = contents.count(patched)
    if original_count + patched_count != 1:
        raise ValueError(
            f'Expected one update URL, found {original_count} original and {patched_count} patched.')
    return contents.index(patched if patched_count else original), bool(patched_count)


def restore(dll):
    if dll.name.lower() != 'spotify.dll' or dll.is_symlink():
        raise ValueError('Select the Spotify.dll file in the Spotify installation folder.')
    contents = dll.read_bytes()
    offset, patched = update_offset(contents)
    if not patched:
        return 'The original update URL is already present.'

    end = offset + len(UPDATE_URL)
    original = contents[:offset] + UPDATE_URL + contents[end:]
    digest = hashlib.sha256(original).hexdigest()
    backup = dll.with_name(f'{dll.name}.floggfy-{digest}.bak')
    if not backup.exists():
        # Earlier native builds used unique startup backup names.
        # Accept a backup only if it matches the expected original bytes.
        for candidate in dll.parent.glob(f'{dll.name}.floggfy-startup-*.bak'):
            if (not candidate.is_symlink() and candidate.is_file()
                    and candidate.stat().st_size == len(original)
                    and candidate.read_bytes() == original):
                backup = candidate
                break
    if backup.is_symlink() or not backup.is_file() or backup.read_bytes() != original:
        raise ValueError(f'Missing or mismatched original backup: {backup}')
    with staged_copy(dll, original) as staged:
        shutil.copymode(dll, staged)
        if dll.read_bytes() != contents:
            raise ValueError('Spotify.dll changed during the scan. Close Spotify and retry.')
        os.replace(staged, dll)
    return f'Restored the update URL at file offset 0x{offset:x}. Backup: {backup}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('restore',))
    parser.add_argument('dll', type=Path, help='path to Spotify.dll (quit Spotify first)')
    args = parser.parse_args()
    try:
        print(restore(args.dll))
    except (OSError, ValueError) as error:
        print(f'Update patch failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
