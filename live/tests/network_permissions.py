#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only in a disposable, network-none container with CAP_NET_ADMIN added."""
import os
from pathlib import Path
import pwd
import stat
import subprocess


def run(*args, status=0):
    result = subprocess.run(args, text=True, capture_output=True, timeout=30)
    assert result.returncode == status, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def main():
    helper = Path('/usr/lib/mnos/net-admin')
    assert helper.stat().st_uid == 0 and stat.S_IMODE(helper.stat().st_mode) == 0o750
    assert 'cap_net_' not in run('/usr/sbin/getcap', str(helper))
    assert Path('/etc/mnos/network-policy').read_text() == 'root\n'
    users = ('mnosnettest', 'mnosnetother')
    for name in users:
        run('/usr/sbin/useradd', '--create-home', '--shell', '/bin/bash', name)
    member = ['runuser', '-u', users[0], '--']
    outsider = ['runuser', '-u', users[1], '--']
    try:
        # Ordinary networking is usable without either sudo or capabilities.
        run(*member, '/usr/bin/nmap', '-sT', '-n', '-Pn', '-p', '1', '127.0.0.1')
        run(*member, 'mnos-net', 'ip', 'link', 'show', status=77)
        run(*member, '/usr/bin/nmap', '-sS', '-n', '-Pn', '-p', '1', '127.0.0.1', status=1)
        run('/usr/sbin/mnos-network-policy', 'netadmin', users[0])
        assert 'cap_net_admin,cap_net_raw=ep' in run('/usr/sbin/getcap', str(helper))
        assert pwd.getpwnam(users[0]).pw_uid >= 1000
        assert not helper.stat().st_mode & (stat.S_ISUID | stat.S_ISGID)
        run(*outsider, 'mnos-net', 'ip', 'link', 'set', 'dev', 'lo', 'mtu', '64000', status=77)
        run(*member, '/usr/sbin/mnos-network-policy', 'root', status=1)
        run(*member, 'mnos-net', '/bin/sh', '-c', 'id', status=2)
        run(*member, 'mnos-net', 'nmap', '-sS', '-n', '-Pn', '-p', '1', '127.0.0.1')
        # Loopback exists even when this host has no loadable dummy-interface driver.
        run(*member, 'mnos-net', 'ip', 'link', 'set', 'dev', 'lo', 'mtu', '64000')
        assert 'mtu 64000' in run(*outsider, '/usr/sbin/ip', 'link', 'show', 'lo')
        run(*member, 'mnos-net', 'ip', 'link', 'set', 'dev', 'lo', 'mtu', '65536')
        run('/usr/sbin/mnos-network-policy', 'root')
        run(*member, 'mnos-net', 'ip', 'link', 'show', status=77)
        assert 'security.capability' not in os.listxattr(helper)
        print('Network policy: default ordinary networking, explicit NET_ADMIN/NET_RAW, '
              'non-root raw scan and link management, other-user denial, tool allowlist and revocation: PASS')
    finally:
        run('/usr/sbin/mnos-network-policy', 'root')
        for name in users:
            run('/usr/sbin/userdel', '--remove', name)


if __name__ == '__main__':
    main()
