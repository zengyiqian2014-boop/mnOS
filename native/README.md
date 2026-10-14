# mnOS native kernel

An early x86_64 kernel written in freestanding C and assembly. It boots directly
through GRUB Multiboot2; no Linux kernel, Linux system calls or host OS runs
underneath it. GRUB, the GNU compiler toolchain and the Noto font are external
components. This directory is original GPL-3.0-or-later code, subject to the
repository license; the font has its own license in `assets/FONT-LICENSE.txt`.

## What runs today

- 64-bit long mode, physical page allocation, per-process page tables.
- Supervisor/user separation; read-only code, non-executable data, null and
  stack guard pages, CR0 write protection, optional SMEP/SMAP.
- GDT, IDT, TSS, interrupt handling, a 100 Hz PIT preemptive scheduler, bounded
  system calls and sleep/wakeup. Idle executes HLT instead of spinning.
- Two ring-3 demonstration processes and six deliberately hostile test processes.
- A Chinese framebuffer UI, a kernel console, PS/2 keyboard/mouse, COM1 serial,
  PCI enumeration and basic ACPI S5 power-off.

This is a development kernel, not a replacement for RHEL or a security-certified
system. It is single-core and uses at most 4 GiB of physical memory. It does not
yet implement a filesystem, USB host controller, network/Wi-Fi driver, audio,
GPU acceleration, a POSIX/Linux ABI, general program loading, SMP or modern CPU
speculation mitigations. PCI enumeration alone does not make devices usable.
Firefox, CTF utilities and normal hardware drivers run in the **separate Linux
tool environment** supplied on both mnOS ISOs. No performance superiority over
Linux/RHEL is claimed without comparable benchmarks.

## Build and test

On x86_64 with GCC, binutils and make:

```sh
make -C native -j4
docker build -t mnos-live-builder -f live/Builder.Dockerfile live
docker run --rm --network none -v "$PWD/native:/native:ro" \
  -v "$PWD/native/build:/out" mnos-live-builder bash /native/pack.sh test
docker run --rm --network none -v "$PWD/native:/native:ro" \
  -v "$PWD/native/build:/out" mnos-live-builder \
  python3 /native/tests/boot.py /out/native-test.iso
```

The test runs actual BIOS and UEFI boots under QEMU TCG, including a qemu64 CPU
without SMEP/SMAP and only 128 MiB RAM. Each boot checks allocator/page-table
invariants, supervisor memory access, NX, a stack guard, invalid syscall
pointers, read-only user code, forbidden port I/O and timer preemption between
distinct address spaces. It also checks sleeping tasks wake while the kernel
uses HLT. Success requires serial `SELFTEST PASS 8/8` **and** QEMU debug-exit 33;
timeout, panic, unexpected faults or a different exit code fail the test.

Use `bash /native/pack.sh demo` instead of `test` for a standalone interactive
ISO. The demo stays running after its checks. The combined ISO is built with
`./live/build.sh qemu` or `./live/build.sh hardware`.

Secure Boot must be disabled for this unsigned experimental kernel. The Linux
entry uses Debian's signed shim, GRUB and Linux packages. Never bypass signature
verification or enroll a downloaded private signing key to boot this kernel.

## Controls

F1–F6 select the six UI pages; `t` opens the console. The mouse can select sidebar
pages. Console commands: `help`, `uname`, `mem`, `tasks`, `pci`, `test`, `uptime`,
`clear`, `reboot`, `shutdown`. `p` opens the power menu outside the console;
R/S/Escape reboot, shut down or cancel. Serial input uses the same commands.
Use PS/2 input in QEMU; this kernel does not drive USB tablets.

The font atlas is checked in so normal builds do not depend on font-generation
tools. To change UI text, regenerate it with Pillow and the Simplified Chinese
face of Noto Sans CJK Regular:

```sh
python3 native/tools/font.py /path/NotoSansCJK-Regular.ttc native/assets/font.c
```

## System-call ABI

`int 0x80`, RAX = operation, RDI/RSI = arguments, RAX = result. Registers other
than RAX are preserved. Calls: 0 bounded console write (pointer, length ≤256),
1 yield, 2 monotonic ticks, 3 exit, 4 boot-probe report (test task only),
5 sleep (1–1000 ticks). Bad addresses return -14; invalid arguments -22;
unknown calls -38. Code/data/stack pages are fixed per task in this prototype.
