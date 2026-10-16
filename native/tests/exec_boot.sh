#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run after building and packing both native test images. No Linux guest kernel.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
log="$root/build/exec-x86-boot.log"
set +e
timeout 90 qemu-system-x86_64 -accel tcg -cpu max -smp 4 -m 512 \
    -display none -serial stdio -monitor none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=4 \
    -cdrom "$root/build/native-test.iso" > "$log" 2>&1
rc=$?
set -e
if [ "$rc" != 33 ]; then cat "$log"; exit 1; fi
grep -q 'Native ELF64 x86_64 loaded' "$log"
grep -q 'SELFTEST PASS 8/8' "$log"
grep -q 'SMP PASS 4/4 processors' "$log"
! grep -q 'PANIC' "$log"
echo 'PASS: x86 ELF user execution, 4-CPU isolation and preemption'
bash "$root/tests/arm64_boot.sh" "$root/build/arm64"
for name in direct-a53-gic2 direct-a72-gic2 direct-max-gic3-el2 direct-high-memory uefi-fdt uefi-acpi; do
    grep -q 'Native ELF64 ARM64 loaded' "$root/build/arm64/arm64-$name.log"
done
echo 'PASS: ARM ELF user execution in all six boot configurations'
