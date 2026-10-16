#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
ARCH=${ARCH:-$(uname -m)}
case "$ARCH" in x86_64) ARCH=amd64 ;; aarch64) ARCH=arm64 ;; amd64|arm64) ;; *) exit 2 ;; esac
ISO=${1:-$ROOT/out/mnOS-2.0-preview-installer-$ARCH.iso}
MEMORY=${MEMORY:-4096}
CPUS=${CPUS:-2}
if [ "$ARCH" = arm64 ]; then
    QEMU=${QEMU:-qemu-system-aarch64}; MACHINE=virt,gic-version=max; VIDEO=virtio-gpu-pci
else
    QEMU=${QEMU:-qemu-system-x86_64}; MACHINE=q35; VIDEO=virtio-vga
fi
for number in "$MEMORY" "$CPUS"; do
    [[ "$number" =~ ^[1-9][0-9]*$ ]] || { echo 'MEMORY and CPUS must be positive integers.' >&2; exit 2; }
done
test -f "$ISO" || { echo "ISO not found: $ISO" >&2; exit 1; }
ACCEL=tcg
CPU=max
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    case "$(uname -m):$ARCH" in x86_64:amd64|aarch64:arm64) ACCEL=kvm; CPU=host ;; esac
fi
if [ "$(uname -s)" = Darwin ]; then
    case "$(uname -m):$ARCH" in arm64:arm64|x86_64:amd64) ACCEL=hvf; CPU=host ;; esac
fi
args=(-accel "$ACCEL" -cpu "$CPU" -smp "$CPUS" -m "$MEMORY" -machine "$MACHINE"
      -boot order=d
      -netdev user,id=net0 -device virtio-net-pci,netdev=net0
      -device virtio-rng-pci -serial mon:stdio)
if [ "$ARCH" = arm64 ]; then
    args+=(-device virtio-scsi-pci,id=scsi -drive "file=$ISO,media=cdrom,if=none,id=cd,format=raw,readonly=on" -device scsi-cd,drive=cd,bootindex=0)
    # ARM virt has no built-in PS/2 controller. Supply real guest input devices.
    args+=(-device qemu-xhci -device usb-kbd -device usb-tablet)
else
    args+=(-cdrom "$ISO")
fi
case "${GPU:-software}" in
    software) args+=(-device "$VIDEO,xres=1920,yres=1080") ;;
    virgl) if [ "$ARCH" = arm64 ]; then VIDEO=virtio-gpu-gl-pci; else VIDEO=virtio-vga-gl; fi
           args+=(-device "$VIDEO,xres=1920,yres=1080" -display gtk,gl=on) ;;
    *) echo 'GPU must be software or virgl.' >&2; exit 2 ;;
esac
if [ -n "${DISK:-}" ]; then
    # Existing disk only. Never create, truncate, or choose a host disk implicitly.
    test -f "$DISK" || { echo 'DISK must name an existing qcow2 file.' >&2; exit 1; }
    args+=(-drive "file=$DISK,format=qcow2,if=virtio,discard=unmap")
fi
if [ "${UEFI:-0}" = 1 ] || [ "$ARCH" = arm64 ]; then
    if [ "$ARCH" = arm64 ]; then
        CODE=${AAVMF_CODE:-/usr/share/AAVMF/AAVMF_CODE.fd}
        VARS_TEMPLATE=${AAVMF_VARS:-/usr/share/AAVMF/AAVMF_VARS.fd}
    else
        CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
        VARS_TEMPLATE=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
    fi
    if [ ! -r "$CODE" ] && [ "$(uname -s)" = Darwin ] && command -v brew >/dev/null; then
        firmware="$(brew --prefix)/share/qemu"
        if [ "$ARCH" = arm64 ]; then
            CODE="$firmware/edk2-aarch64-code.fd"; VARS_TEMPLATE="$firmware/edk2-arm-vars.fd"
        else
            CODE="$firmware/edk2-x86_64-code.fd"; VARS_TEMPLATE="$firmware/edk2-i386-vars.fd"
        fi
    fi
    test -r "$CODE" && test -r "$VARS_TEMPLATE" || { echo 'UEFI firmware not found.' >&2; exit 1; }
    NVRAM=$(mktemp)
    trap 'rm -f "$NVRAM"' EXIT
    cp "$VARS_TEMPLATE" "$NVRAM"
    args+=(-drive "if=pflash,format=raw,readonly=on,file=$CODE" -drive "if=pflash,format=raw,file=$NVRAM")
    "$QEMU" "${args[@]}"
else
    exec "$QEMU" "${args[@]}"
fi
