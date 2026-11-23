#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 022
cd "$(dirname "$0")/.."
FLAVOR=${FLAVOR:-full}
export FLAVOR
case "$FLAVOR" in full) suffix='' ;; lite) suffix='-lite' ;; *) exit 2 ;; esac
ARCHES=${ARCHES:-amd64 arm64}
test -n "$ARCHES" || exit 2
for arch in $ARCHES; do case "$arch" in amd64|arm64) ;; *) echo 'ARCHES accepts amd64 and arm64 only.' >&2; exit 2;; esac; done
mkdir -p build/desktop/amd64 build/desktop/arm64
flags='-O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -fshort-wchar -mgeneral-regs-only -Wall -Wextra -Werror -Wno-misleading-indentation -Iarch/arm64/freestanding -Iinclude'
for arch in $ARCHES; do
if [ "$arch" = amd64 ]; then
make -j4 GUEST="${GUEST:-0}" build/desktop/kernel-x86_64.elf
clang-19 --target=x86_64-unknown-windows $flags -c arch/x86_64/efi.c -o build/desktop/amd64/loader.obj
clang-19 --target=x86_64-unknown-windows $flags -c src/string.c -o build/desktop/amd64/string.obj
clang-19 --target=x86_64-unknown-windows -c arch/x86_64/payload.S -o build/desktop/amd64/kernel.obj
lld-link-19 /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /out:build/desktop/amd64/BOOTX64.EFI build/desktop/amd64/loader.obj build/desktop/amd64/string.obj build/desktop/amd64/kernel.obj
else
make -f arch/arm64/Makefile -j4 GUEST="${GUEST:-0}" build/arm64/BOOTAA64.EFI
cp build/arm64/BOOTAA64.EFI build/desktop/arm64/BOOTAA64.EFI
fi
    case "$arch" in amd64) target=x86_64-unknown-windows; machine=x64; efi=BOOTX64.EFI;; arm64) target=aarch64-unknown-windows; machine=arm64; efi=BOOTAA64.EFI;; esac
    objects=''
    for source in installer/main.c src/string.c src/mnfs.c src/disk_layout.c src/text.c assets/installer-font.c; do
        object="build/desktop/$arch/$(basename "$source" .c).obj"
        clang-19 --target="$target" $flags -c "$source" -o "$object"
        objects="$objects $object"
    done
    clang-19 --target="$target" -DPAYLOAD="\"build/desktop/$arch/$efi\"" -c installer/payload.S -o "build/desktop/$arch/payload.obj"
    lld-link-19 /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:$machine "/out:build/desktop/$arch/INSTALL.EFI" $objects "build/desktop/$arch/payload.obj"
    # The linked installer owns its payload; discard the duplicate before
    # creating the ESP and ISO, both of which also contain the full guest.
    rm -f "build/desktop/$arch/payload.obj"
    work=$(mktemp -d)
    mkdir -p "$work/iso/boot"
    mkdir -p "$work/iso/licenses"
    cp vendor/README.md "$work/iso/licenses/THIRD_PARTY.md"
    cp vendor/monocypher/LICENCE.md "$work/iso/licenses/Monocypher-LICENCE.md"
    cp vendor/qrcodegen/qrcodegen.h "$work/iso/licenses/QR-Code-Generator-NOTICE.h"
    cp assets/*LICENSE.txt "$work/iso/licenses/"
    # The native edition embeds its Linux guest. Size the ISO's boot partition
    # from the actual installer payload instead of silently exceeding 16 MiB.
    bytes=$(wc -c < "build/desktop/$arch/INSTALL.EFI")
    size=$(( (bytes + 8 * 1024 * 1024 + 1048575) / 1048576 ))
    esp="$work/iso/boot/efi.img"
    truncate -s "${size}M" "$esp"
    mkfs.vfat "$esp" >/dev/null
    mmd -i "$esp" ::EFI ::EFI/BOOT
    mcopy -i "$esp" "build/desktop/$arch/INSTALL.EFI" "::EFI/BOOT/$efi"
    printf '%s\n' "$FLAVOR" > "$work/iso/FLAVOR.txt"
    # Large EFI images have an El Torito sector count of zero. EDK2 then
    # derives their extent from the ISO volume size. An appended partition is
    # outside that extent and appears empty. Keep the ESP *inside* the ISO,
    # and point its GPT entry at the same bytes for USB/disk boot as well.
    xorriso -as mkisofs -quiet -R -J -V MNOS_DESKTOP -e boot/efi.img -no-emul-boot -efi-boot-part --efi-boot-image --protective-msdos-label -o "build/desktop/mnOS-0.4-desktop-installer$suffix-$arch.iso" "$work/iso"
    rm -r "$work"
done
(cd build/desktop && sha256sum mnOS-*.iso > SHA256SUMS)
