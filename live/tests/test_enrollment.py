# SPDX-License-Identifier: GPL-3.0-or-later
"""Host checks for the compiled enrollment source; runtime checks run in the ISO image."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class EnrollmentBuildTests(unittest.TestCase):
    def test_compiled_entry_rejects_ordinary_user_without_prompt(self):
        source = Path(__file__).resolve().parents[1] / 'src/first-boot.cpp'
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'first-boot'
            subprocess.run(['c++', '-std=c++17', '-O2', str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], input='', text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('root', result.stderr)
            self.assertNotIn('Password', result.stdout)

if __name__ == '__main__':
    unittest.main()
