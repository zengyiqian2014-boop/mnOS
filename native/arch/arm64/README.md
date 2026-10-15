# Original ARM64 kernel

This is a port of mnOS's freestanding C/assembly kernel. It runs at EL1 after
UEFI `ExitBootServices`, or boots directly from an AArch64 `Image`. Linux does
not run underneath it. The allocator, MNFS, accounts, password hashing, file
commands, fonts and GUI are shared with x86_64. The ARM exception, page-table,
GIC, PSCI, PL011, VirtIO and firmware code is original C/assembly.

## Build and boot

Build/QA dependencies stay on the host. Neither native ISO contains Python,
Linux, Firefox or CTF programs. Those applications remain in the separate
[desktop preview](../../../live/README.md).

From the repository root on an x86_64 or ARM64 Docker host:

```sh
docker build -t mnos-arm-builder -f native/arch/arm64/Builder.Dockerfile .
mkdir -p native/build
docker run --rm --network none -v "$PWD/native:/native" -w /native mnos-arm-builder \
    sh -c 'make -f arch/arm64/Makefile -j4 all build/arm64/BOOTAA64-TEST.EFI &&
           make host-tools &&
           sh arch/arm64/pack.sh demo && sh arch/arm64/pack.sh test &&
           bash tests/arm64_boot.sh &&
           g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/arm64_console.cpp -o build/arm64/console-test &&
           build/arm64/console-test /native/build/arm64'
```

The new ARM tests are **Shell and C++**. `BOOTAA64-TEST.EFI` uses an explicit test
entry point; it runs the same kernel and powers off after all cores pass. The
normal ISO remains interactive. It has no default account/password and never
formats an attached disk. Without an existing MNFS disk, files/accounts are
lost at power-off.

Interactive graphical boot, using QEMU and AAVMF/QEMU_EFI (firmware path varies
between distributions):

```sh
qemu-system-aarch64 -machine virt,gic-version=3,acpi=off -cpu max -smp 4 -m 1G -nic none \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -cdrom native/build/arm64/native-arm64-demo.iso \
  -global virtio-mmio.force-legacy=false \
  -device ramfb -device virtio-keyboard-device -device virtio-rng-device \
  -serial mon:stdio
```

Add an existing MNFS disk with `-drive if=none,id=data,format=raw,file=mnfs.img`
and `-device virtio-blk-device,drive=data`. The disk is mounted read-only for
validation; authenticated users can write their own homes. Write support
requires a VirtIO device with the FLUSH feature. F1–F7 switch GUI pages. Serial
input also works; after signing in, `t` opens the kernel terminal. `help` is
English, `help -cn` is Chinese. GOP keeps the firmware's boot resolution;
wallpaper changes work, but runtime ARM display-mode switching is not implemented.

For a serial-only direct boot replace `-bios` and `-cdrom` with
`-kernel native/build/arm64/Image`. This path accepts a standard FDT and can enter
at EL1 or EL2. Only CPUs enabled in the firmware description are started, via
PSCI. CPU contexts are allocated dynamically rather than indexed by MPIDR.

## Compatibility and test evidence

| Path | Tested configuration |
| --- | --- |
| Direct FDT | Cortex-A53, GICv2, 1 CPU, 256 MiB |
| Direct FDT | Cortex-A72, GICv2, 4 CPUs, 512 MiB |
| Direct FDT, EL2 handoff | QEMU `max`, GICv3, 8 CPUs, 1 GiB |
| Direct FDT, high memory | QEMU `max`, 2 CPUs, 8 GiB; actual read/write above 4 GiB |
| UEFI + FDT | 4 CPUs, GICv3, GOP framebuffer |
| UEFI + ACPI | 4 CPUs, XSDT/MADT/GTDT/SPCR/FADT discovery, GOP framebuffer |
| UEFI interactive | 2 CPUs; VirtIO MMIO keyboard/RNG/block, user/root login, English/Chinese help, protected credentials and homes, files crossing 4 GiB, abrupt power-off and remount |

Each core executes real EL0 probes: kernel-read denial, NX data, stack guard,
bad syscall pointer rejection, read-only code, privileged instruction denial,
and timer preemption of two independent user address spaces. The eighth check
covers page allocation and kernel setup. Every result must appear once per CPU;
exceptions at an unexpected address/class fail the boot.

Physical address handling is capped at the smaller of CPU PARange and 64 TiB.
The existing x86 33-TiB sparse-RAM test is **not** an ARM64 physical-RAM stress
test. Dynamic CPU allocation does not promise unlimited processors. Board RAM
maps, GIC redistributor ranges, PSCI, CPU features and available RAM still apply.

Hardware support requires a 4-KiB-capable AArch64 CPU, PSCI, PL011/SBSA-compatible
UART and GICv2/v3. Firmware must expose FDT or the supported ACPI tables. ACPI
currently supports one GIC redistributor discovery range; FDT input is bounded
and malformed/oversized tables fail closed. These requirements cover useful
virtual and server-style platforms; **no physical board has been verified**.
Raspberry Pi, Rockchip and Apple Silicon are not claimed to work from this ISO
without their respective boot firmware and missing platform drivers.

Native VirtIO drivers currently use modern **MMIO**, not PCI transports. FDT
enumerates them. ACPI-only platforms can boot and use GOP/serial, but do not yet
have an AML interpreter or native PCI/USB/block input drivers. They may seed a
4-KiB, one-use password-salt pool from the UEFI RNG protocol before handoff.
Each consumed byte is erased; pool exhaustion or unavailable entropy refuses
new password hashes. FDT VirtIO RNG requests check returned byte counts and can
replenish entropy. No timestamps or fixed seeds substitute for randomness.

Secure Boot must be disabled for the unsigned original kernel. Native network,
Wi-Fi, USB, accelerated GPU, POSIX/Linux ABI, CPU hotplug, task migration, NUMA
policy and a general program loader remain unimplemented. The Linux desktop
provides those applications/drivers separately. These boot tests are not a
security certification or a performance comparison with RHEL.
