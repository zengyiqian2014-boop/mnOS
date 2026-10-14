#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run inside mnos-live-builder with /native read-only and /out writable.
set -eu
mode=${1:-demo}
case "$mode" in demo) args= ;; test) args=mnos.selftest=1 ;; *) exit 2 ;; esac
grub-file --is-x86-multiboot2 /native/build/mnos.elf
mkdir -p /tmp/native-iso/boot/grub
cp /native/build/mnos.elf /tmp/native-iso/boot/mnos.elf
cat > /tmp/native-iso/boot/grub/grub.cfg <<EOF
set timeout=0
insmod all_video
set gfxmode=1280x720,auto
set gfxpayload=keep
menuentry 'mnOS native 0.1' {
    multiboot2 /boot/mnos.elf $args
    boot
}
EOF
grub-mkrescue -o "/out/native-$mode.iso" /tmp/native-iso
