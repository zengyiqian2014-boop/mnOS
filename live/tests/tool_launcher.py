#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise shipped ELF launchers; test harness is not a runtime dependency."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess

def main():
    machine = {'x86_64': 62, 'aarch64': 183}[platform.machine()]
    for binary in ('/usr/bin/mnos-center', '/usr/bin/mnos-tools', '/usr/lib/mnos/first-boot',
                   '/usr/lib/mnos/wallpaper', '/usr/sbin/mnos-network-policy',
                   '/usr/bin/kernel', '/usr/sbin/mnos-kernel-admin', '/usr/lib/mnos/net-admin',
                   '/usr/bin/mnfs'):
        with Path(binary).open('rb') as executable:
            header = executable.read(20)
        assert header[:6] == b'\x7fELF\x02\x01', binary
        assert int.from_bytes(header[18:20], 'little') == machine, binary
    catalog = json.loads(Path('/usr/share/mnos/tools.json').read_text())
    assert len(catalog) >= 120
    nmap = next(x for x in catalog if x['name'] == 'Nmap')
    ident = hashlib.sha256(nmap['name'].encode()).hexdigest()[:12]
    result = subprocess.run(['mnos-tools', '--describe-tool', ident], text=True, capture_output=True, check=True)
    assert '/usr/bin/xfce4-terminal' in result.stdout
    assert "'/usr/bin/nmap' '--help'" in result.stdout
    result = subprocess.run(['mnos-tools', '--help-tool', ';touch /tmp/mnos-unsafe-launch'], capture_output=True)
    assert result.returncode == 2
    assert not Path('/tmp/mnos-unsafe-launch').exists()
    subprocess.run(['mnos-center', '--self-test'], check=True)
    subprocess.run(['mnos-tools', '--self-test'], check=True)
    print('Compiled menu, catalog availability, XDG apps, search and invalid-ID rejection: PASS')
if __name__ == '__main__':
    main()
