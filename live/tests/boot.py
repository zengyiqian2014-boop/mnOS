#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Boot the complete, unmodified ISO through firmware to a Linux login prompt.

No debug shell, account injection, bootloader edits or external network is used.
Enrollment and graphical interaction require separate checks; see VALIDATION.md.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def boot(iso, output, mode):
    output.mkdir(parents=True, exist_ok=True)
    serial = output / f'linux-{mode}.log'
    serial.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='mnos-boot-') as temporary:
        command = [
            'qemu-system-x86_64', '-accel', 'tcg', '-machine', 'q35,smm=on',
            '-cpu', 'max', '-smp', '2', '-m', '3072', '-device', 'virtio-vga',
            '-device', 'virtio-rng-pci', '-nic', 'none', '-cdrom', str(iso),
            '-boot', 'order=d', '-display', 'none', '-monitor', 'none',
            '-serial', f'file:{serial}', '-no-reboot',
        ]
        if mode != 'bios':
            secure = mode == 'uefi-secure'
            code = 'OVMF_CODE_4M.secboot.fd' if secure else 'OVMF_CODE_4M.fd'
            variables = 'OVMF_VARS_4M.ms.fd' if secure else 'OVMF_VARS_4M.fd'
            nvram = Path(temporary) / 'vars.fd'
            shutil.copyfile(Path('/usr/share/OVMF') / variables, nvram)
            command += [
                '-drive', f'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/{code}',
                '-drive', f'if=pflash,format=raw,file={nvram}',
            ]
        with (output / f'linux-{mode}-qemu.log').open('w') as diagnostics:
            process = subprocess.Popen(command, stdout=diagnostics, stderr=diagnostics)
            try:
                deadline = time.monotonic() + 300
                while time.monotonic() < deadline:
                    text = serial.read_text(errors='replace') if serial.exists() else ''
                    for failure in ('Kernel panic', 'Verification failed', 'Emergency mode',
                                    'prohibited by secure boot policy'):
                        if failure in text:
                            raise AssertionError(f'{mode}: {failure}; see {serial}')
                    if 'mnos-' in text and ' login:' in text:
                        print(f'{mode}: unmodified ISO reached Linux login', flush=True)
                        return
                    if process.poll() is not None:
                        raise AssertionError(f'{mode}: QEMU exited {process.returncode}')
                    time.sleep(1)
                raise AssertionError(f'{mode}: no login after 300 seconds; see {serial}')
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=Path('/out/boot-checks'))
    parser.add_argument('--mode', choices=('all', 'bios', 'uefi', 'uefi-secure'), default='all')
    args = parser.parse_args()
    iso = args.iso.resolve(strict=True)
    for mode in ('bios', 'uefi', 'uefi-secure') if args.mode == 'all' else (args.mode,):
        boot(iso, args.output.resolve(), mode)
