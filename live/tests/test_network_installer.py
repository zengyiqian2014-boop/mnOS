# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class NetworkInstallerTests(unittest.TestCase):
    def test_compiled_installer_job_fails_closed_for_missing_selection(self):
        text = (ROOT / 'overlay/etc/calamares/modules/mnosnetwork.conf').read_text()
        self.assertIn('dontChroot: false', text)
        self.assertIn('root: /usr/sbin/mnos-network-policy root', text)
        self.assertIn('netadmin: /usr/sbin/mnos-network-policy netadmin ${USER}', text)
        self.assertIn('"*": /usr/bin/false', text)
        self.assertNotIn('interface: python', text)
        script = (ROOT / 'configure.sh').read_text()
        self.assertIn('contextualprocess@mnosnetwork', script)
        self.assertIn('module: contextualprocess', script)
if __name__ == '__main__':
    unittest.main()
