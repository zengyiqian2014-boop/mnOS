#!/bin/bash
# Host-side QA only; native runtime is freestanding C and assembly.
set -euo pipefail
out=$(cd "${1:-build/arm64}" && pwd)
run() {
    local name=$1 cpu=$2 cores=$3 ram=$4 machine=$5 firmware=$6
    local log="$out/arm64-$name.log"
    local args=(-machine "$machine" -cpu "$cpu" -smp "$cores" -m "$ram"
        -accel tcg -nic none -display none -serial stdio -monitor none -no-reboot
        -global virtio-mmio.force-legacy=false)
    if [ "$firmware" = uefi ]; then
        args+=(-bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd
            -cdrom "$out/native-arm64-test.iso" -device ramfb)
        if [[ "$machine" = *acpi=on* ]]; then args+=(-device virtio-rng-pci)
        else args+=(-device virtio-rng-device); fi
    else
        args+=(-kernel "$out/Image" -append mnos.selftest=1 -device virtio-rng-device)
    fi
    timeout 120 qemu-system-aarch64 "${args[@]}" > "$log" 2>&1 || { cat "$log"; return 1; }
    if grep -q 'PANIC:' "$log"; then cat "$log"; return 1; fi
    grep -q "ARM BOOT PASS: $cores CPUs" "$log"
    for ((cpu_id=0;cpu_id<cores;cpu_id++)); do
        test "$(grep -cx "ARM CPU $cpu_id SELFTEST PASS 8/8" "$log")" = 1
    done
    for probe in 2 3 4 6 7; do
        test "$(grep -cx "ARM EL0 fault probe PASS $probe" "$log")" = "$cores"
    done
    test "$(grep -c 'ARM syscall pointer rejection PASS' "$log")" = "$cores"
    test "$(grep -c 'ARM timer preemption PASS' "$log")" = "$cores"
    grep -q 'FILESYSTEM PASS: MNFS' "$log"
    if [ "$firmware" = uefi ]; then grep -q 'UEFI ExitBootServices completed' "$log"; fi
    if [[ "$machine" = *acpi=on* ]]; then grep -q 'ARM platform: ACPI' "$log"; fi
    if [ "$ram" = 8G ]; then grep -q 'ARM high RAM read/write PASS' "$log"; fi
    printf 'PASS %s: %s CPUs; EL0 isolation, timer preemption and MNFS\n' "$name" "$cores"
}
run direct-a53-gic2 cortex-a53 1 256M virt,gic-version=2 direct
run direct-a72-gic2 cortex-a72 4 512M virt,gic-version=2 direct
run direct-max-gic3-el2 max 8 1G virt,gic-version=3,virtualization=on direct
run direct-high-memory max 2 8G virt,gic-version=3 direct
run uefi-fdt max 4 1G virt,gic-version=3,acpi=off uefi
run uefi-acpi max 4 1G virt,gic-version=3,acpi=on uefi
