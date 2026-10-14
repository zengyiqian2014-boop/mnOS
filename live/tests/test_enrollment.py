#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import importlib.machinery
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / 'overlay/usr/lib/mnos/first-boot'
loader = importlib.machinery.SourceFileLoader('enrollment', str(SOURCE))
spec = importlib.util.spec_from_loader(loader.name, loader)
enrollment = importlib.util.module_from_spec(spec)
loader.exec_module(enrollment)


class EnrollmentTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.marker = Path(self.tmp.name) / 'state' / 'complete'
        marker_patch = patch.object(enrollment, 'MARKER', self.marker)
        marker_patch.start()
        self.addCleanup(marker_patch.stop)
        self.commands = []

    def command(self, args, **kwargs):
        self.commands.append((args, kwargs))
        return subprocess.CompletedProcess(args, 1 if args[0] == 'getent' else 0, '', '')

    def test_rejects_non_root(self):
        with patch.object(enrollment.os, 'geteuid', return_value=1000):
            with self.assertRaises(SystemExit):
                enrollment.enroll()
        self.assertFalse(self.marker.exists())

    def test_username_validation_password_not_logged_and_marker_private(self):
        secret = 'Test-only-passphrase-7294'
        output = io.StringIO()
        with patch.object(enrollment.os, 'geteuid', return_value=0), \
             patch('builtins.input', side_effect=['bad;name', 'validuser']), \
             patch.object(enrollment.getpass, 'getpass', return_value=secret), \
             patch.object(enrollment.subprocess, 'run', side_effect=self.command), \
             contextlib.redirect_stdout(output):
            enrollment.enroll()
        self.assertEqual(self.marker.read_text(), 'validuser\n')
        self.assertEqual(self.marker.stat().st_mode & 0o777, 0o600)
        self.assertNotIn(secret, output.getvalue())
        adds = [args for args, kw in self.commands if args[0] == 'useradd']
        self.assertEqual(len(adds), 1)
        self.assertEqual(adds[0][-1], 'validuser')
        for args, kw in self.commands:
            self.assertNotIn(secret, args)
            self.assertFalse(kw.get('shell', False))

    def test_cancellation_does_not_create_account_or_marker(self):
        with patch.object(enrollment.os, 'geteuid', return_value=0), \
             patch('builtins.input', side_effect=EOFError), \
             patch.object(enrollment.subprocess, 'run', side_effect=self.command), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(EOFError):
                enrollment.enroll()
        self.assertEqual(self.commands, [])
        self.assertFalse(self.marker.exists())

    def test_password_failure_removes_incomplete_account(self):
        def fail_password(args, **kwargs):
            if args[0] == 'chpasswd':
                raise subprocess.CalledProcessError(1, args)
            return self.command(args, **kwargs)
        with patch.object(enrollment.os, 'geteuid', return_value=0), \
             patch('builtins.input', side_effect=['validuser', EOFError]), \
             patch.object(enrollment.getpass, 'getpass', return_value='Test-only-passphrase-7294'), \
             patch.object(enrollment.subprocess, 'run', side_effect=fail_password), \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(EOFError):
                enrollment.enroll()
        self.assertIn(['userdel', '--remove', 'validuser'], [cmd for cmd, kw in self.commands])
        self.assertFalse(self.marker.exists())

    def test_completed_setup_does_not_prompt_again(self):
        self.marker.parent.mkdir()
        self.marker.write_text('existing\n')
        with patch.object(enrollment.os, 'geteuid', return_value=0), \
             patch('builtins.input', side_effect=AssertionError('unexpected prompt')):
            enrollment.enroll()


if __name__ == '__main__':
    unittest.main()
