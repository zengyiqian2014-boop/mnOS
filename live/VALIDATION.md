# Validation record

## Native kernel

Tested locally under QEMU TCG on 2026-10-15:

| Boot | CPU | RAM | Result |
| --- | --- | --- | --- |
| BIOS | max (SMEP/SMAP) | 512 MiB | 8/8, sleep/wakeup, HLT idle |
| BIOS | qemu64 | 128 MiB | 8/8, sleep/wakeup, HLT idle |
| UEFI / OVMF | max | 512 MiB | 8/8, sleep/wakeup, HLT idle |
| BIOS, 4 CPUs | max, xAPIC | 512 MiB | All CPUs online; per-CPU isolation/preemption/GS-reset checks passed |
| BIOS, 8 and 32 CPUs | max, x2APIC | 512 MiB | All CPUs completed 8/8 and HLT/sleep checks |
| UEFI, 4 CPUs | max, x2APIC | 512 MiB | Per-CPU checks passed |
| BIOS, APIC IDs 0 and 256 | max, q35 | 512 MiB | Two CPUs online, sparse 32-bit ID preserved |
| BIOS, 1 GiB pages disabled | max | 8 GiB | 2 MiB fallback and high physical reads/writes passed |
| BIOS, physical width 46 bits | max, q35 | 33 TiB sparse | Reads/writes above 32 TiB and at last usable page; isolation passed |

PS/2 function keys, mouse page selection, console commands and ACPI power-off
also passed in a booted VM. Test programs, failure conditions and command lines
are in `native/tests/`. UEFI validation here uses Secure Boot off.

The 33 TiB run uses non-preallocated, non-reserved virtual memory and touches a
small subset of pages. It validates address handling, not full-RAM stress or
physical server performance. Host allocator tests additionally exercise the
64 TiB boundary, ownership/double-free rejection, exhaustion and randomized maps
under ASan/UBSan. No fixed CPU-count array is used; tests do not establish
unbounded CPU scalability or NUMA performance.

MNFS host tests passed >4 GiB writes, >32 TiB sparse offsets, zero holes,
overflow rejection, tree splits, directories/rename, snapshot/collection,
read-only remount, ENOSPC rollback and corruption detection. Fault injection
covers 12 write/flush/torn-write boundaries with write-through storage and 12
with a reordered volatile cache. Damaged latest metadata falls back to an older
checkpoint; two damaged roots are rejected. These tests run with ASan/UBSan.

The native ATA VM test wrote persistent files, crossed 4 GiB, saved/restored a
snapshot and display settings, cut VM power, rebooted and authenticated, and verified
the same contents with the host MNFS utility. Both the standard VGA BIOS and
VirtIO VGA OVMF UI tests passed `help`/`help -cn`, `kernel help`/`kernel help -cn`,
F1–F7, mouse navigation, wallpaper changes and shutdown. Mode cycling covered
1024×768, 1280×720, 1600×900 and 1920×1080 on both adapters, plus 2560×1440 on
standard VGA. VirtIO's 8 MiB legacy framebuffer skips the oversized mode rather
than trapping the resolution selector.
Screen captures and serial logs are retained as test artifacts. Native GPU
support here is 2D; physical GPU acceleration is not validated.

The combined desktop ISO's native entry also passed 8/8 through BIOS and OVMF
after adding the EFI Multiboot2 modules from the same GRUB package version as
the signed EFI core. The standalone native ISO did not expose that packaging
defect. `live/tests/boot.py` now tests the combined entry in both firmware modes.
Its temporary ISO changes only the default menu selection and timeout; kernel,
EFI binaries, modules and menu-entry commands are unchanged.

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
| Polkit from the graphical session | Password prompt and authenticated `pkexec /usr/bin/id` worked |
| Local Cockpit | System overview loaded in the local management window |
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
BIOS, UEFI and Secure Boot Linux boots, and combined-ISO native BIOS/UEFI boots
before publishing either ISO. Linux checks use the unmodified release image;
native checks use the menu-selection fixture described above.
Detailed boot logs are retained in the workflow artifacts.

## Accounts, network permissions and development tools

Local checks on 2026-10-15 for the next preview passed:

- Native SHA-256/PBKDF2 vectors and account state/permission tests under
  ASan/UBSan: enrollment, hidden passwords, retry delays, ordinary/root roles,
  password changes, additional accounts, root expiry, damaged-record rejection
  and random-source failure. No default account password is shipped.
- Native BIOS and VirtIO/OVMF UI tests: real keyboard enrollment followed by
  ordinary-user page navigation, bilingual help, display modes and wallpaper.
- Native ATA reboot test: ordinary home writes, denial of outside-home writes
  and private-file reads, root authentication, volume lock, snapshot restore
  requiring reauthentication, persistent settings and successful reboot login.
  Python's independent PBKDF2 implementation verifies the stored hashes; test
  passwords are absent from serial logs and the stored credential record.
- Linux normal-user queries and administrative-operation rejection, including
  direct calls to the privileged entry point, spoofed environment and mismatched
  real/effective UIDs. Root mutations are exercised against isolated fixtures.
- In a disposable network-isolated container: ordinary Nmap connect scanning,
  default denial of raw scanning, opt-in raw SYN scanning on loopback, NET_ADMIN
  loopback MTU changes, denial for a second account, and capability revocation.
  The container receives NET_ADMIN solely to make that test possible.
- Compiled C++ enrollment, tested through a real pseudo-terminal: cancellation,
  invalid username, hidden password input, password hash verification, private
  setup marker and no second prompt after completion. Network-policy C++
  runtime passes the capability/delegation/revocation test above.

The workflow additionally validates the installed Calamares page/job order and
default choice. `development.py` runs small C, C++, Rust, Go, Java, JavaScript
programs as an ordinary user without networking; it also checks
nano, the official VS Code CLI and the remaining build tools. CLI checks alone
do not prove that the editor's graphical window or every extension works.
These checks run against both newly built profiles before publishing. The installer now uses Calamares' compiled contextualprocess job with a fail-closed fallback and independent
account validation in the C++ backend. This is not a complete disk install test.

The installer is included, but an end-to-end disk installation, encrypted
installation and subsequent disk boot have not been validated here. Back up
data and test on a disposable virtual disk before using the installer on a PC.

No physical PC, Wi-Fi radio or GPU has been tested in this environment. Firmware
and kernel module inclusion is not a substitute for testing a specific device.
TCG results establish functionality; they are not physical-hardware performance
benchmarks or a security certification.
