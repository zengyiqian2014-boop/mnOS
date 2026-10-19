#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Native-only diagnostic media; the full installation ISO is built by live/.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${OUT:-/out}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mode=${1:-demo}
case "$mode" in demo) args= ;; test) args=mnos.selftest=1 ;; *) exit 2 ;; esac
grub-file --is-x86-multiboot2 "$root/build/mnos.elf"
mkdir -p "$work/boot/grub" "$out"
cp "$root/build/mnos.elf" "$work/boot/mnos.elf"
cat > "$work/boot/grub/grub.cfg" <<EOF
set timeout=0
insmod all_video
set gfxmode=1280x720,auto
set gfxpayload=keep
menuentry 'mnOS native 0.4 diagnostic' {
    multiboot2 /boot/mnos.elf $args
    boot
}
EOF
grub-mkrescue -o "$out/native-$mode.iso" "$work"
