#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Round-trip transport splitting without allocating a multi-gigabyte fixture."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'release-assets.sh'


class ReleaseAssetsTests(unittest.TestCase):
    def test_split_roundtrip_and_stale_output_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in [('large.iso', bytes(range(256))), ('small.iso', b'unchanged')]:
                (root / name).write_bytes(data)
                (root / (name + '.sha256')).write_text(hashlib.sha256(data).hexdigest() + '  ' + name + '\n')
            env = {**os.environ, 'MNOS_RELEASE_MAX_BYTES': '128', 'MNOS_RELEASE_CHUNK_BYTES': '100'}
            subprocess.run(['bash', str(SCRIPT), directory], env=env, check=True, capture_output=True)
            output = root / 'publish'
            self.assertFalse((output / 'large.iso').exists())
            parts = sorted(output.glob('large.iso.part[0-9][0-9]'))
            self.assertEqual(len(parts), 3)
            self.assertEqual(b''.join(part.read_bytes() for part in parts), bytes(range(256)))
            self.assertEqual((output / 'small.iso').read_bytes(), b'unchanged')
            subprocess.run(['sha256sum', '--check', 'large.iso.parts.sha256'],
                           cwd=output, check=True, capture_output=True)
            result = subprocess.run(['bash', str(SCRIPT), directory], env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
