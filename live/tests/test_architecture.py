#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-side launcher checks; never boot or write a physical device."""
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

LIVE = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def launch(self, arch, uefi=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            iso = root / 'image with spaces.iso'
            iso.touch()
            firmware = root / 'firmware.fd'
            firmware.write_bytes(b'template-must-not-change')
            record = root / 'arguments'
            fake = root / 'qemu'
            fake.write_text('#!/bin/sh\nprintf "%s\\0" "$@" > "$MNOS_QA_ARGS"\n')
            fake.chmod(0o700)
            env = {**os.environ, 'ARCH': arch, 'QEMU': str(fake),
                   'MNOS_QA_ARGS': str(record), 'GPU': 'software',
                   'MEMORY': '4096', 'CPUS': '2', 'UEFI': str(int(uefi)),
                   'AAVMF_CODE': str(firmware), 'AAVMF_VARS': str(firmware),
                   'OVMF_CODE': str(firmware), 'OVMF_VARS': str(firmware)}
            env.pop('DISK', None)
            subprocess.run(['bash', str(LIVE / 'run.sh'), str(iso)], env=env, check=True)
            self.assertEqual(firmware.read_bytes(), b'template-must-not-change')
            args = record.read_text().rstrip('\0').split('\0')
            writable = [arg for arg in args if arg.startswith('if=pflash,format=raw,file=')]
            for arg in writable:
                self.assertFalse(Path(arg.split('file=', 1)[1]).exists())
            return args

    def test_x86_bios_and_uefi(self):
        bios = self.launch('amd64')
        self.assertIn('q35', bios)
        self.assertIn('-cdrom', bios)
        self.assertIn('virtio-vga,xres=1920,yres=1080', bios)
        self.assertFalse(any('pflash' in arg for arg in bios))
        self.assertTrue(any('pflash' in arg for arg in self.launch('amd64', True)))

    def test_arm_requires_uefi_and_virtio_cd(self):
        args = self.launch('arm64')
        self.assertIn('virt,gic-version=max', args)
        self.assertIn('scsi-cd,drive=cd,bootindex=0', args)
        self.assertIn('virtio-gpu-pci,xres=1920,yres=1080', args)
        self.assertTrue(any('pflash' in arg for arg in args))
        self.assertNotIn('-cdrom', args)
        if platform.machine() != 'aarch64':
            self.assertEqual(args[args.index('-accel') + 1], 'tcg')

    def test_unknown_architecture_fails_before_build(self):
        result = subprocess.run(['bash', str(LIVE / 'build.sh'), 'qemu', 'not-an-arch'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('Unsupported architecture', result.stderr)


if __name__ == '__main__':
    unittest.main()
