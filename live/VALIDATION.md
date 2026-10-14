# Validation record

## Native kernel

Tested locally under QEMU TCG on 2026-10-14:

| Boot | CPU | RAM | Result |
| --- | --- | --- | --- |
| BIOS | max (SMEP/SMAP) | 512 MiB | 8/8, sleep/wakeup, HLT idle |
| BIOS | qemu64 | 128 MiB | 8/8, sleep/wakeup, HLT idle |
| UEFI / OVMF | max | 512 MiB | 8/8, sleep/wakeup, HLT idle |

PS/2 function keys, mouse page selection, console commands and ACPI power-off
also passed in a booted VM. Test programs, failure conditions and command lines
are in `native/tests/`. UEFI validation here uses Secure Boot off.

## Linux companion

Five enrollment tests, the GTK window/page/tool checks, and 41 Linux kernel
configuration checks passed. Combined ISO boot, graphical login and runtime
security checks are being validated before a release tag is created.

No physical PC, Wi-Fi radio or GPU has been tested in this environment. Firmware
and kernel module inclusion is not a substitute for testing a specific device.
TCG results establish functionality; they are not physical-hardware performance
benchmarks or a security certification.
