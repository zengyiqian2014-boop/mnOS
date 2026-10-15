#!/usr/bin/env python3
"""Sparse virtual RAM tests; these are not physical-capacity benchmarks."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('iso', type=Path)
parser.add_argument('--out', type=Path, default=Path('/out'))
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
for name, memory, cpu, minimum in (
    ('8g-2m', '8G', 'max,phys-bits=46,pdpe1gb=off', 1 << 32),
    ('33t-1g', '33T', 'max,phys-bits=46', 1 << 45),
):
    command = ['qemu-system-x86_64', '-accel', 'tcg', '-cpu', cpu,
               '-object', f'memory-backend-ram,id=ram,size={memory},reserve=off,prealloc=off,merge=off,dump=off',
               '-machine', 'q35,memory-backend=ram', '-m', memory,
               '-cdrom', str(args.iso), '-display', 'none', '-serial', 'stdio',
               '-monitor', 'none', '-nic', 'none', '-no-reboot',
               '-device', 'isa-debug-exit,iobase=0xf4,iosize=4']
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    except subprocess.TimeoutExpired as error:
        (args.out / f'native-memory-{name}.log').write_bytes(error.stdout or b'')
        raise
    output = result.stdout.decode(errors='replace')
    (args.out / f'native-memory-{name}.log').write_text(output)
    assert result.returncode == 33 and 'SELFTEST PASS 8/8' in output, output
    high = re.search(r'HIGHMEM PASS physical=(0x[0-9a-f]+)', output)
    assert high and int(high[1], 16) >= minimum, output
    count = re.search(r'managed_pages=(\d+)', output)
    assert count and int(count[1]) * 4096 > minimum, output
    assert 'address_limit_GiB=65536' in output, output
    if name == '8g-2m': assert 'huge_1G=0' in output, output
    print(f'PASS {name}: high physical reads/writes, allocator and ring-3 isolation', flush=True)
