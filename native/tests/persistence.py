#!/usr/bin/env python3
"""Use the actual ATA driver and console, then remount the same image after power loss."""
import argparse
import hashlib
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

            def secret(text, expected=None):
                # Password input must never enter the console history or retained log.
                for character in (text + '\n').encode():
                    connection.sendall(bytes([character]))
                    time.sleep(.015)
                if expected:
                    wait(expected, timeout=180)

            def login(name, password):
                secret(name)
                secret(password, 'AUTH: Signed in as an ordinary user.' if name != 'root' else 'AUTH: Root session:')

            wait('SMP PASS 4/4')
        wait('/disk validated read-only')
            if phase == 1:
                secret('demo')
                secret('ordinarytest491')
                secret('ordinarytest491', 'Set a separate password for root')
                secret('rootadmintest785')
                secret('rootadmintest785', 'Accounts created. Signed in as the ordinary user.')
            else:
                login('demo', 'ordinarytest491')
                wait('SETTINGS: restored wallpaper=1')
                wait('DISPLAY: 1600x900 / DISPI')
            connection.sendall(b't')
            wait('UI: page 4')
            command('cat /disk/readme', 'pre-existing-file')
            if phase == 1:
                command('write /disk/outside-home blocked', 'Permission denied')
                command('disk unlock', 'Permission denied')
                command('mkdir /disk/home/demo/work')
                command('write /disk/home/demo/message persisted-data', 'written and flushed')
                command('writeat /disk/home/demo/large 4294967294 cross4g', 'written and flushed')
                command('su')
                secret('rootadmintest785', 'AUTH: Root session:')
                command('disk lock', '/disk read-only')
                command('write /disk/forbidden blocked', 'read-only')
                command('disk unlock', '/disk writes enabled')
                command('write /disk/root/secret administrator-only', 'written and flushed')
                command('snapshot save /disk', 'snapshot committed')
                command('write /disk/home/demo/message overwritten', 'written and flushed')
                command('snapshot restore /disk', 'snapshot committed')
                login('demo', 'ordinarytest491')
                command('kernel display 1600x900')
                wait('DISPLAY: 1600x900 / DISPI')
                command('kernel wallpaper 1')
                command('kernel save-settings')
                wait('SETTINGS: saved')
            command('cat /disk/root/secret', 'Permission denied')
            command('cat /disk/system/accounts.bin', 'Permission denied')
            command('cat /disk/home/demo/message', 'persisted-data')
            command('cat /disk/home/demo/large 4294967294', 'cross4g')
            command('stat /disk/home/demo/large', 'Size: 4294967301')
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
            assert 'ordinarytest491' not in output and 'rootadmintest785' not in output
    result = subprocess.run([str(tool), str(disk), 'cat', '/home/demo/message'], capture_output=True, check=True)
    assert result.stdout == b'persisted-data'
    settings = subprocess.run([str(tool), str(disk), 'cat', '/home/demo/display.cfg'], capture_output=True, check=True)
    assert settings.stdout == b'1600 900 1\n'
    denied = subprocess.run([str(tool), str(disk), 'stat', '/forbidden'], capture_output=True)
    assert denied.returncode != 0
    accounts = subprocess.check_output([str(tool), str(disk), 'cat', '/system/accounts.bin'])
    assert len(accounts) == 1360
    for index, password in enumerate((b'rootadmintest785', b'ordinarytest491')):
        record = accounts[16 + index * 84:16 + (index + 1) * 84]
        assert hashlib.pbkdf2_hmac('sha256', password, record[36:52], 600000) == record[52:84]
        assert password not in accounts
    subprocess.run([str(tool), str(disk), 'check'], check=True)
print('PASS native ATA: account persistence, ordinary-user writes, root permissions, protected credentials, >4 GiB file, snapshot reauthentication, display preferences and power loss')
