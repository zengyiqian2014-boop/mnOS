#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
ISO=${1:-$ROOT/out/mnOS-2.0-preview-qemu-amd64.iso}
MEMORY=${MEMORY:-4096}
CPUS=${CPUS:-2}
QEMU=${QEMU:-qemu-system-x86_64}
for number in "$MEMORY" "$CPUS"; do
    [[ "$number" =~ ^[1-9][0-9]*$ ]] || { echo 'MEMORY and CPUS must be positive integers.' >&2; exit 2; }
done
test -f "$ISO" || { echo "ISO not found: $ISO" >&2; exit 1; }
ACCEL=tcg
CPU=max
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then ACCEL=kvm; CPU=host; fi
args=(-accel "$ACCEL" -cpu "$CPU" -smp "$CPUS" -m "$MEMORY" -machine q35
      -cdrom "$ISO" -boot order=d -device virtio-vga
      -netdev user,id=net0 -device virtio-net-pci,netdev=net0
      -device virtio-rng-pci -serial mon:stdio)
if [ -n "${DISK:-}" ]; then
    # Existing disk only. Never create, truncate, or choose a host disk implicitly.
    test -f "$DISK" || { echo 'DISK must name an existing qcow2 file.' >&2; exit 1; }
    args+=(-drive "file=$DISK,format=qcow2,if=virtio,discard=unmap")
fi
if [ "${UEFI:-0}" = 1 ]; then
    CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
    VARS_TEMPLATE=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
    test -r "$CODE" && test -r "$VARS_TEMPLATE" || { echo 'OVMF firmware not found.' >&2; exit 1; }
    NVRAM=$(mktemp)
    trap 'rm -f "$NVRAM"' EXIT
    cp "$VARS_TEMPLATE" "$NVRAM"
    args+=(-drive "if=pflash,format=raw,readonly=on,file=$CODE" -drive "if=pflash,format=raw,file=$NVRAM")
    "$QEMU" "${args[@]}"
else
    exec "$QEMU" "${args[@]}"
fi
