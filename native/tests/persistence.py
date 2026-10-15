#!/usr/bin/env python3
"""Use the actual ATA driver and console, then remount the same image after power loss."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('iso', type=Path)
parser.add_argument('--out', type=Path, default=Path('/out'))
args = parser.parse_args()
tool = args.out / 'mnfs'
with tempfile.TemporaryDirectory(dir=args.out) as temporary:
    directory = Path(temporary)
    disk = directory / 'disk.img'
    subprocess.run([str(tool), 'format', str(disk), '32'], check=True)
    subprocess.run([str(tool), str(disk), 'create', '/readme'], check=True)
    subprocess.run([str(tool), str(disk), 'write', '/readme', 'pre-existing-file'], check=True)
    for phase in (1, 2):
        serial, qmp = directory / f'serial{phase}', directory / f'qmp{phase}'
        vm = subprocess.Popen(['qemu-system-x86_64', '-accel', 'tcg', '-cpu', 'max', '-smp', '4',
            '-m', '512', '-cdrom', str(args.iso), '-boot', 'order=d', '-display', 'none', '-monitor', 'none',
            '-drive', f'file={disk},format=raw,if=ide,index=0,cache=writeback',
            '-serial', f'unix:{serial},server=on,wait=on', '-qmp', f'unix:{qmp},server=on,wait=off', '-no-reboot'])
        output = ''
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(.1)
        try:
            end = time.monotonic() + 15
            while not serial.exists():
                assert vm.poll() is None and time.monotonic() < end
                time.sleep(.05)
            connection.connect(str(serial))

            def wait(text, timeout=45):
                global output
                end = time.monotonic() + timeout
                while text not in output:
                    assert vm.poll() is None, output
                    assert time.monotonic() < end, f'Missing {text}: {output}'
                    try:
                        data = connection.recv(65536)
                        if data:
                            output += data.decode(errors='replace')
                    except TimeoutError:
                        pass
                assert 'PANIC:' not in output, output

            def command(text, expected=None):
                global output
                start = len(output)
                output += f'\nTEST SEND: {text}\n'
                for character in (text + '\n').encode():
                    connection.sendall(bytes([character]))
                    time.sleep(.015)
                wait(f'CONSOLE: {text}\n')
                if expected:
                    wait('FS: ' + expected)
                    assert 'FS: ' + expected in output[start:], output[start:]

            wait('SMP PASS 4/4')
            wait('/disk mounted read-only')
            if phase == 2:
                wait('SETTINGS: restored wallpaper=1')
                wait('DISPLAY: 1600x900 / DISPI')
            connection.sendall(b't')
            wait('UI: page 4')
            command('cat /disk/readme', 'pre-existing-file')
            if phase == 1:
                command('write /disk/forbidden blocked', 'read-only')
                command('disk unlock', '/disk writes enabled')
                command('mkdir /disk/home')
                command('write /disk/home/message persisted-data', 'written and flushed')
                command('writeat /disk/home/large 4294967294 cross4g', 'written and flushed')
                command('snapshot save /disk', 'snapshot committed')
                command('write /disk/home/message overwritten', 'written and flushed')
                command('snapshot restore /disk', 'snapshot committed')
                command('kernel display 1600x900')
                wait('DISPLAY: 1600x900 / DISPI')
                command('kernel wallpaper 1')
                command('kernel save-settings')
                wait('SETTINGS: saved')
            command('cat /disk/home/message', 'persisted-data')
            command('cat /disk/home/large 4294967294', 'cross4g')
            command('stat /disk/home/large', 'Size: 4294967301')
            # QMP quit cuts power without a guest shutdown; completed writes must persist.
            control = socket.socket(socket.AF_UNIX)
            control.connect(str(qmp))
            stream = control.makefile('rwb', buffering=0)
            stream.readline()
            for request in ('qmp_capabilities', 'quit'):
                stream.write((json.dumps({'execute': request}) + '\n').encode())
                while 'return' not in json.loads(stream.readline()):
                    pass
            vm.wait(timeout=10)
            stream.close()
            control.close()
        finally:
            connection.close()
            if vm.poll() is None:
                vm.terminate()
                vm.wait(timeout=5)
            (args.out / f'native-files-phase{phase}.log').write_text(output)
    result = subprocess.run([str(tool), str(disk), 'cat', '/home/message'], capture_output=True, check=True)
    assert result.stdout == b'persisted-data'
    settings = subprocess.run([str(tool), str(disk), 'cat', '/display.cfg'], capture_output=True, check=True)
    assert settings.stdout == b'1600 900 1\n'
    denied = subprocess.run([str(tool), str(disk), 'stat', '/forbidden'], capture_output=True)
    assert denied.returncode != 0
    subprocess.run([str(tool), str(disk), 'check'], check=True)
print('PASS native ATA: read-only gate, writes, >4 GiB file, snapshot, power loss and host remount')
