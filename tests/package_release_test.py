"""Exercise package failures without modifying the real release files."""
from pathlib import Path
import os
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve()
        source = Path(__file__).resolve().parents[1]
        self.script = self.root / 'package-release.py'
        shutil.copyfile(source / self.script.name, self.script)
        for name in ('README.md', 'AUDIO-HISTORY.md', 'LICENSE', 'SpotifyHistory.ini', 'VERSION'):
            shutil.copyfile(source / name, self.root / name)
        for name in ('docs', 'native/vendor'):
            shutil.copytree(source / name, self.root / name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(source / 'tools/spotify_update_patch.py',
                        self.root / 'tools/spotify_update_patch.py')

        self.build = self.root / 'build'
        self.build.mkdir()
        (self.build / 'version.dll').write_bytes(b'new build')
        (self.build / 'Floggfy.exe').write_bytes(b'new launcher')
        self.dist = self.root / 'dist'
        self.dist.mkdir()
        self.dll = self.dist / 'version.dll'
        version = (self.root / 'VERSION').read_text(encoding='ascii').strip()
        self.archive = self.dist / f'Floggfy-v{version}-Launcher-Windows-x64.zip'
        self.old_files = {
            self.archive.name: b'previous same-version launcher archive',
            'Floggfy-v1.3-Launcher-Windows-x64.zip': b'previous launcher archive',
            self.dll.name: b'previous packaged dll',
        }
        for name, contents in self.old_files.items():
            (self.dist / name).write_bytes(contents)

    def assert_previous_files_preserved(self):
        self.assertEqual({path.name for path in self.dist.iterdir()}, self.old_files.keys())
        for name, contents in self.old_files.items():
            self.assertEqual((self.dist / name).read_bytes(), contents, name)

    def run_with_blocked_replacements(self, *destinations):
        real_replace = os.replace
        blocked_destinations = iter(destinations)
        blocked = next(blocked_destinations)

        def replace(source, target):
            nonlocal blocked
            if Path(target) == blocked:
                blocked = next(blocked_destinations, None)
                raise PermissionError(f'injected sharing violation at {target}')
            return real_replace(source, target)

        with patch.object(os, 'replace', replace), patch.object(sys, 'argv', [str(self.script)]):
            runpy.run_path(str(self.script), run_name='__main__')

    def test_missing_build_input_preserves_previous_release(self):
        (self.build / 'Floggfy.exe').unlink()
        result = subprocess.run([sys.executable, str(self.script)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Floggfy.exe', result.stderr)
        self.assert_previous_files_preserved()

    def test_launcher_archive_contains_update_patch(self):
        subprocess.run([sys.executable, str(self.script)], check=True, capture_output=True)
        name = 'tools/spotify_update_patch.py'
        with zipfile.ZipFile(self.archive) as archive:
            self.assertEqual(archive.read(name), (self.root / name).read_bytes())

    def test_failed_dll_replacement_preserves_previous_release(self):
        with self.assertRaisesRegex(PermissionError, 'injected sharing violation'):
            self.run_with_blocked_replacements(self.dll)
        self.assert_previous_files_preserved()

    def test_failed_archive_replacement_restores_previous_dll(self):
        with self.assertRaisesRegex(PermissionError, 'injected sharing violation'):
            self.run_with_blocked_replacements(self.archive)
        self.assert_previous_files_preserved()

    def test_failed_archive_replacement_removes_new_dll(self):
        self.dll.unlink()
        del self.old_files[self.dll.name]
        with self.assertRaisesRegex(PermissionError, 'injected sharing violation'):
            self.run_with_blocked_replacements(self.archive)
        self.assert_previous_files_preserved()

    def test_failed_dll_rollback_keeps_the_previous_dll_recoverable(self):
        with self.assertRaisesRegex(RuntimeError, 'Could not restore') as error:
            self.run_with_blocked_replacements(self.archive, self.dll)
        (staging,) = (path for path in self.dist.iterdir() if path.is_dir())
        self.assertIn(str(staging), str(error.exception))
        self.assertIn(self.old_files[self.dll.name], (path.read_bytes() for path in staging.iterdir()))
        self.assertEqual(self.archive.read_bytes(), self.old_files[self.archive.name])


if __name__ == '__main__':
    unittest.main()
