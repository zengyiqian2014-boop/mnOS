#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Boot both environments through the bootloader shipped in the complete ISO.

Linux uses the unmodified ISO. Native checks use a temporary copy with ONLY the
default menu selection and timeout changed. Kernel, EFI binaries, modules and
menu-entry commands remain exactly as published. No account injection or
external network is used. See VALIDATION.md for separate interaction checks.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def boot(iso, output, mode, target='linux', arch='amd64'):
    output.mkdir(parents=True, exist_ok=True)
    serial = output / f'{target}-{mode}.log'
    serial.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='mnos-boot-') as temporary:
        command = [
            'qemu-system-x86_64', '-accel', 'tcg', '-machine', 'q35,smm=on',
            '-cpu', 'max', '-smp', '1' if target == 'native' else '2',
            '-m', '512' if target == 'native' else '3072', '-device', 'virtio-vga',
            '-device', 'virtio-rng-pci', '-nic', 'none', '-cdrom', str(iso),
            '-boot', 'order=d', '-display', 'none', '-monitor', 'none',
            '-serial', f'file:{serial}', '-no-reboot',
        ]
        if arch == 'arm64':
            assert target == 'linux' and mode == 'uefi'
            nvram = Path(temporary) / 'vars.fd'
            shutil.copyfile('/usr/share/AAVMF/AAVMF_VARS.fd', nvram)
            command = [
                'qemu-system-aarch64', '-accel', 'tcg', '-machine', 'virt,gic-version=3',
                '-cpu', 'max', '-smp', '2', '-m', '3072', '-device', 'virtio-gpu-pci',
                '-device', 'virtio-rng-pci', '-nic', 'none',
                '-device', 'virtio-scsi-pci,id=scsi',
                '-drive', f'file={iso},media=cdrom,if=none,id=cd,format=raw,readonly=on',
                '-device', 'scsi-cd,drive=cd,bootindex=0',
                '-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd',
                '-drive', f'if=pflash,format=raw,file={nvram}',
                '-display', 'none', '-monitor', 'none', '-serial', f'file:{serial}', '-no-reboot',
            ]
        elif mode != 'bios':
            secure = mode == 'uefi-secure'
            code = 'OVMF_CODE_4M.secboot.fd' if secure else 'OVMF_CODE_4M.fd'
            variables = 'OVMF_VARS_4M.ms.fd' if secure else 'OVMF_VARS_4M.fd'
            nvram = Path(temporary) / 'vars.fd'
            shutil.copyfile(Path('/usr/share/OVMF') / variables, nvram)
            command += [
                '-drive', f'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/{code}',
                '-drive', f'if=pflash,format=raw,file={nvram}',
            ]
        with (output / f'{target}-{mode}-qemu.log').open('w') as diagnostics:
            process = subprocess.Popen(command, stdout=diagnostics, stderr=diagnostics)
            try:
                limit = 90 if target == 'native' else 480
                deadline = time.monotonic() + limit
                while time.monotonic() < deadline:
                    text = serial.read_text(errors='replace') if serial.exists() else ''
                    for failure in ('Kernel panic', 'PANIC', 'SELFTEST FAIL', 'Verification failed', 'Emergency mode',
                                    'prohibited by secure boot policy'):
                        if failure in text:
                            raise AssertionError(f'{mode}: {failure}; see {serial}')
                    if target == 'native' and 'SELFTEST PASS 8/8' in text:
                        print(f'{mode}: combined ISO native entry passed 8/8', flush=True)
                        return
                    if target == 'linux' and 'mnos-' in text and ' login:' in text:
                        print(f'{mode}: unmodified ISO reached Linux login', flush=True)
                        return
                    if process.poll() is not None:
                        raise AssertionError(f'{mode}: QEMU exited {process.returncode}')
                    time.sleep(1)
                raise AssertionError(f'{target}/{mode}: no success after {limit} seconds; see {serial}')
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def native_fixture(iso, directory):
    config = directory / 'grub.cfg'
    selected = directory / 'native-selected.iso'
    with (directory / 'remaster.log').open('w') as log:
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                        '-extract', '/boot/grub/grub.cfg', str(config)],
                       check=True, stdout=log, stderr=log)
        text = config.read_text()
        assert text.count('set default=0') == 1
        assert "menuentry 'mnOS native kernel 0.1 - Secure Boot off'" in text
        assert 'multiboot2 /boot/mnos.elf' in text
        config.chmod(0o600)
        config.write_text(text.replace('set default=0', 'set default=1', 1)
                          .replace('set timeout=5', 'set timeout=1', 1))
        subprocess.run(['xorriso', '-indev', str(iso), '-outdev', str(selected),
                        '-boot_image', 'any', 'replay', '-map', str(config),
                        '/boot/grub/grub.cfg', '-commit'],
                       check=True, stdout=log, stderr=log)
    return selected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=Path('/out/boot-checks'))
    parser.add_argument('--mode', choices=('all', 'bios', 'uefi', 'uefi-secure', 'native'), default='all')
    parser.add_argument('--arch', choices=('amd64', 'arm64'), default='amd64')
    args = parser.parse_args()
    iso = args.iso.resolve(strict=True)
    if args.arch == 'arm64':
        if args.mode not in ('all', 'uefi'):
            parser.error('ARM64 currently validates Linux UEFI only')
        boot(iso, args.output.resolve(), 'uefi', arch='arm64')
        raise SystemExit(0)
    if args.mode != 'native':
        for mode in ('bios', 'uefi', 'uefi-secure') if args.mode == 'all' else (args.mode,):
            boot(iso, args.output.resolve(), mode)
    if args.mode in ('all', 'native'):
        with tempfile.TemporaryDirectory(prefix='mnos-native-entry-') as temporary:
            selected = native_fixture(iso, Path(temporary))
            for mode in ('bios', 'uefi'):
                boot(selected, args.output.resolve(), mode, 'native')
