#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check installed bin/sbin commands as real UIDs, then mutate only a chroot fixture."""
import errno
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

FRONT = '/usr/bin/kernel'
ADMIN = '/usr/sbin/mnos-kernel-admin'
USER = ['runuser', '-u', 'nobody', '--']


def run(*args, status=0, **kwargs):
    result = subprocess.run(args, capture_output=True, text=True, timeout=20, **kwargs)
    assert result.returncode == status, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def copy_executable(source, root):
    """Include the binary's actual loader/libraries; do not depend on host paths in chroot."""
    deps = subprocess.check_output(['ldd', source], text=True)
    for file in {source, *re.findall(r'(/[\w/+.\-]+)', deps)}:
        target = root / file.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def main():
    assert os.getuid() == 0, 'Run in the disposable built rootfs, as root.'
    for binary in (FRONT, ADMIN):
        info = Path(binary).stat()
        assert info.st_uid == info.st_gid == 0, binary
        assert stat.S_IMODE(info.st_mode) == 0o755, binary
        try:
            capability = os.getxattr(binary, 'security.capability')
        except OSError as error:
            assert error.errno in (errno.ENODATA, errno.ENOTSUP), error
        else:
            assert not capability, (binary, 'must not carry file capabilities')

    for prefix in ([], USER):
        assert 'kernel <instruction>' in run(*prefix, FRONT, 'help')
        assert 'kernel <指令>' in run(*prefix, FRONT, 'help', '-cn')
        for instruction in ('info', 'memory', 'cpu', 'gpu', 'devices'):
            run(*prefix, FRONT, instruction)
        if Path('/proc/modules').exists():
            run(*prefix, FRONT, 'modules')
        assert 'Linux' in run(*prefix, FRONT, 'sysctl', 'kernel.ostype')
        run(*prefix, FRONT, 'unknown', status=2)
        run(*prefix, FRONT, 'sysctl', '../proc', status=2)
        run(*prefix, FRONT, 'info', 'unexpected', status=2)

    # Restricted reads must retain the OS decision, not silently retry as root.
    for instruction, underlying in [('log', ['/usr/bin/dmesg', '--color=never']),
                                     ('irq', ['/usr/bin/cat', '/proc/interrupts'])]:
        direct = subprocess.run([*USER, *underlying], capture_output=True)
        wrapped = subprocess.run([*USER, FRONT, instruction], capture_output=True)
        assert wrapped.returncode == direct.returncode, (instruction, direct, wrapped)
        assert wrapped.returncode != 77, 'read-only dispatch must not impose a root gate'

    policy = ['kernel.dmesg_restrict', 'kernel.kptr_restrict']
    before = [run('/usr/sbin/sysctl', '-n', key) for key in policy]
    mutations = [('load', 'dummy'), ('unload', 'dummy'),
                 ('set', 'kernel.domainname=mnos-denied'), ('cpu-online', '1'),
                 ('cpu-offline', '1'), ('performance', 'balanced')]
    for binary in (FRONT, ADMIN):
        for instruction, obj in mutations:
            run(*USER, binary, instruction, obj, status=77)
        # An environment claiming to be root must not substitute for credentials.
        run(*USER, '/usr/bin/env', 'USER=root', 'LOGNAME=root', 'SUDO_UID=0',
            'PATH=/does-not-exist', binary, 'set', 'kernel.domainname=mnos-denied', status=77)
    # Reject a mismatched real/effective UID even if the backend were made setuid.
    run('setpriv', '--ruid=65534', '--euid=0', '--clear-groups', ADMIN,
        'set', 'kernel.domainname=mnos-denied', status=77)
    assert before == [run('/usr/sbin/sysctl', '-n', key) for key in policy]

    # Exercise successful privileged dispatch without changing the running kernel.
    with tempfile.TemporaryDirectory(prefix='mnos-permissions-') as temp:
        root = Path(temp)
        for binary in (FRONT, ADMIN):
            copy_executable(binary, root)
        fixture = root / 'stub.c'
        fixture.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (getenv("MNOS_TEST_POISON") || !getenv("PATH") || !getenv("LANG") ||
        strcmp(getenv("PATH"), "/usr/sbin:/usr/bin:/sbin:/bin") ||
        strcmp(getenv("LANG"), "C.UTF-8")) return 90;
    for (int i = 0; i < argc; ++i) puts(argv[i]);
    return 0;
}
''')
        stub = root / 'stub'
        run('cc', '-Wall', '-Wextra', '-Werror', str(fixture), '-o', str(stub))
        # libc/loader were copied above for the real binaries, built with the same toolchain.
        for name in ('modprobe', 'sysctl', 'tuned-adm'):
            shutil.copy2(stub, root / 'usr/sbin' / name)
        online = root / 'sys/devices/system/cpu/cpu1/online'
        online.parent.mkdir(parents=True)
        online.write_text('1')
        environment = {**os.environ, 'MNOS_TEST_POISON': 'must-be-cleared', 'PATH': '/does-not-exist'}
        chroot = shutil.which('chroot')
        cases = [
            ('load', 'dummy', ['/usr/sbin/modprobe', '--', 'dummy']),
            ('unload', 'dummy', ['/usr/sbin/modprobe', '--remove', '--', 'dummy']),
            ('set', 'kernel.domainname=example;literal',
             ['/usr/sbin/sysctl', '--write', '--', 'kernel.domainname=example;literal']),
        ]
        for profile, target in [('balanced', 'desktop'), ('throughput', 'throughput'), ('virtual', 'virtual')]:
            cases.append(('performance', profile, ['/usr/sbin/tuned-adm', 'profile', 'mnos-' + target]))
        for binary in (FRONT, ADMIN):
            for instruction, obj, expected in cases:
                assert run(chroot, temp, binary, instruction, obj, env=environment).splitlines() == expected
            run(chroot, temp, binary, 'cpu-offline', '1')
            assert online.read_text() == '0'
            run(chroot, temp, binary, 'cpu-online', '1')
            assert online.read_text() == '1'
            invalid = [('cpu-offline', '0'), ('cpu-online', '../1'),
                       ('cpu-online', '4294967296'), ('load', '--help'),
                       ('unload', 'x;touch /unsafe'), ('set', 'x'),
                       ('set', 'kernel.domainname=x\ny'), ('performance', 'unknown')]
            for instruction, obj in invalid:
                run(chroot, temp, binary, instruction, obj, status=2)
            run(chroot, temp, binary, 'set', status=2)
            run(chroot, temp, binary, 'set', 'a=b', 'extra', status=2)
            assert online.read_text() == '1'
            assert not (root / 'unsafe').exists()

    print('User queries, restricted OS reads, bin/sbin credentials, denied mutations, '
          'root dispatch, CPU fixture and argument/environment isolation: PASS')


if __name__ == '__main__':
    main()
