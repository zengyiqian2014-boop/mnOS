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

Local QEMU TCG checks on 2026-10-14:

| Check | Result |
| --- | --- |
| Complete QEMU-profile ISO, BIOS | Linux serial login reached |
| Complete QEMU-profile ISO, OVMF UEFI | Linux serial login reached |
| OVMF with Microsoft keys and Secure Boot enabled | Signed Linux chain booted to login |
| First boot, local keyboard | Administrator created with a chosen password |
| LightDM / Xfce | Graphical login and screen lock/unlock worked |
| Ordinary account / sudo | Normal serial login and authenticated elevation worked |
| `mnos-security-check` | 13 baseline checks passed |
| `systemctl --failed` | No failed units |
| TuneD / network | `mnos-virtual` active; VirtIO connection configured |
| Firefox ESR | Started as the ordinary user and displayed the local guide |
| pwntools / mitmproxy | Import and version checks passed |
| Hashcat | PoCL CPU OpenCL device enumerated |
| Enrollment unit tests | 5 passed, including validation and failure cleanup |
| Linux configuration checks | 41 hardening/driver requirements passed |
| GTK checks | Six pages, tool availability, argument forwarding and SVG rendering passed |

These were development-build checks. The GUI session was used to find missing
SVG and power-panel packages; their installation fixes are in the build recipe.
The boot tests caught a missing optical UEFI configuration and now boot the
corrected image without editing its menu. The release workflow rebuilds both
profiles from its tagged source and repeats native tests, package/GTK checks,
and BIOS, UEFI and Secure Boot Linux boots before publishing either ISO.
Detailed boot logs are retained in the workflow artifacts.

The installer is included, but an end-to-end disk installation, encrypted
installation and subsequent disk boot have not been validated here. Back up
data and test on a disposable virtual disk before using the installer on a PC.

No physical PC, Wi-Fi radio or GPU has been tested in this environment. Firmware
and kernel module inclusion is not a substitute for testing a specific device.
TCG results establish functionality; they are not physical-hardware performance
benchmarks or a security certification.
