#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mode=${1:-demo}
case "$mode" in demo) efi=BOOTAA64.EFI ;; test) efi=BOOTAA64-TEST.EFI ;; *) exit 2 ;; esac
out="$root/build/arm64"
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT
mkdir -p "$work/iso/EFI/BOOT"
cp "$out/$efi" "$work/iso/EFI/BOOT/BOOTAA64.EFI"
truncate -s 64M "$work/esp.img"
mkfs.vfat "$work/esp.img" >/dev/null
mmd -i "$work/esp.img" ::EFI ::EFI/BOOT
mcopy -i "$work/esp.img" "$out/$efi" ::EFI/BOOT/BOOTAA64.EFI
xorriso -as mkisofs -quiet -R -J -V MNOS_ARM64 \
    -append_partition 2 0xef "$work/esp.img" -appended_part_as_gpt \
    -e '--interval:appended_partition_2:all::' -no-emul-boot \
    -o "$out/native-arm64-$mode.iso" "$work/iso"
(cd "$out" && sha256sum "native-arm64-$mode.iso" > "native-arm64-$mode.iso.sha256")
