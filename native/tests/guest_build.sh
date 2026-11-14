#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Reuse the same object directory: changing GUEST must relink every kernel.
set -euo pipefail
cd "$(dirname "$0")/.."
test -s build/guest/Image && test -s build/guest/machine.dtb
payload_size=$(stat -c %s build/guest/Image)
dtb_size=$(stat -c %s build/guest/machine.dtb)
check_extent() {
    local file=$1 prefix=$2 expected=$3 start end
    start=$(nm -P "$file" | awk -v name="${prefix}_start" '$1 == name {print $3}')
    end=$(nm -P "$file" | awk -v name="${prefix}_end" '$1 == name {print $3}')
    test -n "$start" && test -n "$end"
    test "$((16#$end - 16#$start))" -eq "$expected"
}
for guest in 0 1 0 1; do
    make -s -j4 GUEST="$guest" all build/desktop/kernel-x86_64.elf
    make -s -f arch/arm64/Makefile -j4 GUEST="$guest" all build/arm64/BOOTAA64-TEST.EFI
    for file in build/mnos.elf build/desktop/kernel-x86_64.elf build/arm64/mnos.elf; do
        check_extent "$file" linux_image "$((guest * payload_size))"
        check_extent "$file" linux_dtb "$((guest * dtb_size))"
    done
    efi_size=$(stat -c %s build/arm64/BOOTAA64.EFI)
    test_size=$(stat -c %s build/arm64/BOOTAA64-TEST.EFI)
    if [ "$guest" = 0 ]; then
        bare_efi=$efi_size
        bare_test=$test_size
    else
        # PE section alignment may change; both loaders must contain the guest.
        test "$((efi_size - bare_efi))" -ge "$((payload_size - 4096))"
        test "$((test_size - bare_test))" -ge "$((payload_size - 4096))"
    fi
    printf 'GUEST BUILD PASS: GUEST=%s, both architectures and EFI loaders\n' "$guest"
done
