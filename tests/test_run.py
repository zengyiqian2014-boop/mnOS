"""Exercise the launcher without starting a VM."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class RunTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.path = Path(self.work.name)
        self.repo = self.path / 'checkout with spaces'
        (self.repo / 'etc').mkdir(parents=True)
        shutil.copy2(ROOT / 'run.sh', self.repo / 'run.sh')
        (self.repo / 'etc/os-release').write_text('VERSION="test"\n')
        self.iso = self.path / 'mnOSvtest.iso'
        self.iso.touch()
        self.args = self.path / 'arguments'
        self.qemu = self.path / 'fake qemu'
        self.qemu.write_text('#!/bin/sh\nprintf \'%s\\n\' "$@" > "$QEMU_ARGS"\n')
        self.qemu.chmod(0o755)
        self.env = dict(os.environ, QEMU=str(self.qemu), QEMU_ARGS=str(self.args))

    def run_launcher(self, *args):
        return subprocess.run([str(self.repo / 'run.sh'), *args], cwd=self.path,
                              env=self.env, text=True, capture_output=True)

    def test_serial_default_iso_and_memory(self):
        result = self.run_launcher('--serial')
        self.assertEqual(result.returncode, 0, result.stderr)
        accel = 'kvm' if os.access('/dev/kvm', os.R_OK | os.W_OK) else 'tcg'
        self.assertEqual(self.args.read_text().splitlines(),
                         ['-accel', accel, '-m', '1024', '-cdrom', str(self.iso), '-nographic'])

    def test_graphical_custom_iso_and_memory(self):
        iso = self.path / 'custom image.iso'
        iso.touch()
        result = self.run_launcher('--memory', '2048', str(iso))
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.args.read_text().splitlines()
        self.assertEqual(args[2:], ['-m', '2048', '-cdrom', str(iso)])
        self.assertNotIn('-nographic', args)

    def test_help_does_not_require_qemu_or_iso(self):
        self.iso.unlink()
        self.env['QEMU'] = '/not-installed/qemu'
        result = self.run_launcher('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('usage:', result.stdout)

    def test_bad_arguments_do_not_launch_qemu(self):
        for args in [('--unknown',), ('--memory',), ('--memory', '0'),
                     ('--memory', '-1'), ('--memory', 'abc'), ('one', 'two'),
                     ('--', 'one', 'two')]:
            with self.subTest(args=args):
                result = self.run_launcher(*args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('error:', result.stderr)
                self.assertFalse(self.args.exists())

    def test_missing_iso(self):
        result = self.run_launcher('missing.iso')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ISO not found:', result.stderr)
        self.assertFalse(self.args.exists())

    def test_missing_qemu(self):
        self.env['QEMU'] = '/not-installed/qemu'
        result = self.run_launcher()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('QEMU not found:', result.stderr)

    def test_iso_after_option_separator(self):
        iso = self.path / '-custom.iso'
        iso.touch()
        result = self.run_launcher('--', iso.name)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.args.read_text().splitlines()[-1], iso.name)

    def test_qemu_exit_status(self):
        self.qemu.write_text('#!/bin/sh\nexit 7\n')
        self.assertEqual(self.run_launcher().returncode, 7)


if __name__ == '__main__':
    unittest.main()
