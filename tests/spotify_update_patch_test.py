"""Verify legacy update patch recovery against disposable DLL fixtures."""
import hashlib
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'spotify_update_patch.py'
ORIGINAL = b'https://spclient.wg.spotify.com/desktop-update/v2/update'
LOOPBACK = b'https://127.0.0.1/desktop-updates/are/not/desired/thanks'
PE = b'MZ' + bytes(58) + (64).to_bytes(4, 'little') + b'PE\0\0' + bytes(18) + b'\0\x20'


class SpotifyUpdatePatchTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.dll = Path(scratch.name) / 'Spotify.dll'
        self.original = PE + b'prefix' * 19 + ORIGINAL + b'\0suffix'
        self.dll.write_bytes(self.original)

    def run_restore(self, ok=True):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), 'restore', str(self.dll)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stderr)
        return result

    def backup(self):
        digest = hashlib.sha256(self.original).hexdigest()
        return self.dll.with_name(f'Spotify.dll.floggfy-{digest}.bak')

    def patch_fixture(self):
        self.backup().write_bytes(self.original)
        self.dll.write_bytes(self.original.replace(ORIGINAL, LOOPBACK))

    def test_restore_and_retry(self):
        self.patch_fixture()
        self.run_restore()
        self.run_restore()
        self.assertEqual(self.dll.read_bytes(), self.original)
        self.assertEqual(self.backup().read_bytes(), self.original)

    def test_missing_or_ambiguous_url_leaves_file_untouched(self):
        for data in (PE, self.original + ORIGINAL + b'\0',
                     self.original + LOOPBACK + b'\0', PE + ORIGINAL + b'/next\0'):
            with self.subTest(data=data):
                self.dll.write_bytes(data)
                self.run_restore(ok=False)
                self.assertEqual(self.dll.read_bytes(), data)
                self.assertEqual(list(self.dll.parent.iterdir()), [self.dll])

    def test_existing_conflicting_backup_is_preserved(self):
        self.patch_fixture()
        self.backup().write_bytes(b'bad backup')
        self.run_restore(ok=False)
        self.assertEqual(self.backup().read_bytes(), b'bad backup')
        self.assertEqual(self.dll.read_bytes(), self.original.replace(ORIGINAL, LOOPBACK))

    def test_restore_refuses_bytes_that_do_not_match_backup(self):
        self.patch_fixture()
        changed = self.dll.read_bytes() + b'unrelated modification'
        self.dll.write_bytes(changed)
        self.run_restore(ok=False)
        self.assertEqual(self.dll.read_bytes(), changed)

    def test_restore_accepts_matching_native_backup(self):
        native_backup = self.dll.with_name('Spotify.dll.floggfy-startup-123-456.bak')
        native_backup.write_bytes(self.original)
        self.dll.write_bytes(self.original.replace(ORIGINAL, LOOPBACK))
        self.run_restore()
        self.assertEqual(self.dll.read_bytes(), self.original)

    def test_failed_restore_preserves_current_dll_and_backup(self):
        self.patch_fixture()
        restore = runpy.run_path(str(SCRIPT))['restore']
        with patch.object(os, 'replace', side_effect=PermissionError('sharing violation')):
            with self.assertRaises(PermissionError):
                restore(self.dll)
        self.assertEqual(self.dll.read_bytes(), self.original.replace(ORIGINAL, LOOPBACK))
        self.assertEqual(self.backup().read_bytes(), self.original)
        self.assertEqual(set(self.dll.parent.iterdir()), {self.dll, self.backup()})
        self.run_restore()


if __name__ == '__main__':
    unittest.main()
