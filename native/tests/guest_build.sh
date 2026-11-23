#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Reuse the same object directory: changing GUEST must relink every kernel.
set -euo pipefail
cd "$(dirname "$0")/.."
read -r -a arches <<< "${ARCHES-amd64 arm64}"
test "${#arches[@]}" -gt 0
for arch in "${arches[@]}"; do
    case "$arch" in amd64|arm64) ;; *) echo 'ARCHES accepts amd64 and arm64 only.' >&2; exit 2;; esac
done
test -s build/guest/Image && test -s build/guest/machine.dtb && test -s build/guest/rootfs.squashfs
payload_size=$(stat -c %s build/guest/Image)
dtb_size=$(stat -c %s build/guest/machine.dtb)
root_size=$(stat -c %s build/guest/rootfs.squashfs)
check_extent() {
    local file=$1 prefix=$2 expected=$3 start end
    start=$(nm -P "$file" | awk -v name="${prefix}_start" '$1 == name {print $3}')
    end=$(nm -P "$file" | awk -v name="${prefix}_end" '$1 == name {print $3}')
    test -n "$start" && test -n "$end"
    test "$((16#$end - 16#$start))" -eq "$expected"
}
for guest in 0 1 0 1; do
    for arch in "${arches[@]}"; do
        if [ "$arch" = amd64 ]; then
            make -s -j4 GUEST="$guest" all build/desktop/kernel-x86_64.elf
            files=(build/mnos.elf build/desktop/kernel-x86_64.elf)
        else
            make -s -f arch/arm64/Makefile -j4 GUEST="$guest" all build/arm64/BOOTAA64-TEST.EFI
            files=(build/arm64/mnos.elf)
        fi
        for file in "${files[@]}"; do
            check_extent "$file" linux_image "$((guest * payload_size))"
            check_extent "$file" linux_dtb "$((guest * dtb_size))"
            check_extent "$file" linux_root "$((guest * root_size))"
        done
        if [ "$arch" = arm64 ]; then
            efi_size=$(stat -c %s build/arm64/BOOTAA64.EFI)
            test_size=$(stat -c %s build/arm64/BOOTAA64-TEST.EFI)
            if [ "$guest" = 0 ]; then
                bare_efi=$efi_size
                bare_test=$test_size
            else
                # PE section alignment may change; both loaders must contain the guest.
                test "$((efi_size - bare_efi))" -ge "$((payload_size + dtb_size + root_size - 12288))"
                test "$((test_size - bare_test))" -ge "$((payload_size + dtb_size + root_size - 12288))"
            fi
        fi
        printf 'GUEST BUILD PASS: GUEST=%s, architecture=%s\n' "$guest" "$arch"
    done
done
