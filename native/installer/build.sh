#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
cd "$(dirname "$0")/.."
make -j4 GUEST="${GUEST:-0}" all
make -f arch/arm64/Makefile -j4 GUEST="${GUEST:-0}" all
mkdir -p build/desktop/amd64 build/desktop/arm64
make GUEST="${GUEST:-0}" build/desktop/kernel-x86_64.elf
flags='-O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -fshort-wchar -mgeneral-regs-only -Wall -Wextra -Werror -Wno-misleading-indentation -Iarch/arm64/freestanding -Iinclude'
clang-19 --target=x86_64-unknown-windows $flags -c arch/x86_64/efi.c -o build/desktop/amd64/loader.obj
clang-19 --target=x86_64-unknown-windows $flags -c src/string.c -o build/desktop/amd64/string.obj
clang-19 --target=x86_64-unknown-windows -c arch/x86_64/payload.S -o build/desktop/amd64/kernel.obj
lld-link-19 /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /out:build/desktop/amd64/BOOTX64.EFI build/desktop/amd64/loader.obj build/desktop/amd64/string.obj build/desktop/amd64/kernel.obj
cp build/arm64/BOOTAA64.EFI build/desktop/arm64/BOOTAA64.EFI
for arch in amd64 arm64; do
    case "$arch" in amd64) target=x86_64-unknown-windows; machine=x64; efi=BOOTX64.EFI;; arm64) target=aarch64-unknown-windows; machine=arm64; efi=BOOTAA64.EFI;; esac
    objects=''
    for source in installer/main.c src/string.c src/mnfs.c src/disk_layout.c src/text.c assets/installer-font.c; do
        object="build/desktop/$arch/$(basename "$source" .c).obj"
        clang-19 --target="$target" $flags -c "$source" -o "$object"
        objects="$objects $object"
    done
    clang-19 --target="$target" -DPAYLOAD="\"build/desktop/$arch/$efi\"" -c installer/payload.S -o "build/desktop/$arch/payload.obj"
    lld-link-19 /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:$machine "/out:build/desktop/$arch/INSTALL.EFI" $objects "build/desktop/$arch/payload.obj"
    work=$(mktemp -d)
    mkdir -p "$work/iso/EFI/BOOT"
    cp "build/desktop/$arch/INSTALL.EFI" "$work/iso/EFI/BOOT/$efi"
    # The native edition embeds its Linux guest. Size the ISO's boot partition
    # from the actual installer payload instead of silently exceeding 16 MiB.
    bytes=$(wc -c < "build/desktop/$arch/INSTALL.EFI")
    size=$(( (bytes + 8 * 1024 * 1024 + 1048575) / 1048576 ))
    truncate -s "${size}M" "$work/esp.img"
    mkfs.vfat "$work/esp.img" >/dev/null
    mmd -i "$work/esp.img" ::EFI ::EFI/BOOT
    mcopy -i "$work/esp.img" "build/desktop/$arch/INSTALL.EFI" "::EFI/BOOT/$efi"
    xorriso -as mkisofs -quiet -R -J -V MNOS_DESKTOP -append_partition 2 0xef "$work/esp.img" -appended_part_as_gpt -e '--interval:appended_partition_2:all::' -no-emul-boot -o "build/desktop/mnOS-0.4-desktop-installer-$arch.iso" "$work/iso"
    rm -r "$work"
done
(cd build/desktop && sha256sum mnOS-*.iso > SHA256SUMS)
