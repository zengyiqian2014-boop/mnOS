# Validation record

## Installer preview.2 — 2026-10-16

The complete installation media were validated in
[run 38890080005](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38890080005).
The base system comes from `03dc433830e2613589f014e549d54f2aad5be2bd`;
the final installer overlay and verification code come from
`5b253da1eedd1f96c0e885e548b4bd23eafcce3c`.
Both architecture image-build steps and the original-kernel randomness job
succeeded in [source build 38884198603](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38884198603).
The final verification checks each remastered ISO's SHA256 before and after
its VM tests. The base filesystem and installer overlay are also listed in
the ISO's internal checksum manifest.

| Check | amd64 | ARM64 |
| --- | --- | --- |
| Select the second boot entry and render the compatibility installer | Passed, KVM | Passed, TCG |
| GUI installation to a blank 40 GiB disk, with a 64-character username and a two-character test password | Passed, KVM | Passed, TCG |
| Remove ISO, reset UEFI variables, and boot the installed VirtIO disk | Passed, KVM | Passed, TCG |
| Move the installed disk to NVMe and boot with fresh UEFI variables | Passed, KVM | Passed, TCG |
| Actual serial and graphical authentication, ordinary-user session, and sudo shutdown | Passed on both disk controllers | Passed on both disk controllers |
| Rendered launcher panel in the saved desktop capture | Observed on both disk controllers | Observed on NVMe; first VirtIO capture incomplete |
| BIOS boot from installed disk to the graphical login screen | Passed, KVM | Not applicable |

These are offline QEMU 10 tests with two guest CPUs, 4 GiB RAM, VirtIO graphics
and a virtual RNG. ARM uses the `virt-9.1` machine model; this is not a claim to
have tested the UTM application or the QEMU 9.1 executable. Acceleration for each
phase is recorded in `hypervisor.txt` in the published validation archive.
Manual review found that the first ARM VirtIO desktop capture contains a black
rectangle and an unpainted top band. Authentication, the desktop processes and
sudo succeeded, and the subsequent NVMe boot shows the launcher panel, but the
VirtIO capture does not establish a fully rendered first-boot desktop. The
original pixel check accepted this incomplete frame because it only checked
for a dark top band. The repository now also requires painted launcher icons;
that stricter check was not used for this release's verification run. The
visual limitation is retained in the release notes and evidence archive.
The test credentials exist only in the disposable VM; the installer ships no
default account password. Installed-system assertions check the ext4 root,
absence of `boot=live`, a normal user ID, the installed-system marker, the
selected NET_ADMIN/NET_RAW delegation, and Firefox, VS Code, nano and nmap
executables. They do not establish interactive testing of every application.

Account preflight and compiled enrollment checks passed against both base
filesystems: long names, short and 511-byte passwords, rejection beyond the
hash component's boundary, occupied account/group names, hidden password input,
hashed storage and an unchanged system account database during preflight.
The installer no longer imposes its former 31-character username or 12–512
password/strength policy. The underlying tools still accept usernames up to
255 bytes and nonempty passwords up to 511 bytes; unlimited authentication
input has not been implemented.

Console login now lets `login`/PAM collect the username directly, avoiding
agetty's separate 42-byte prompt restriction. Both virtual-console and serial
service configurations are installed to the target disk. Authentication is
retained, with no autologin or PAM bypass. A separate C++ PTY test in disposable
Debian containers passed long-name login, wrong-password rejection, hidden
input and entry into an ordinary-user shell.

The compatibility entry has its own `mnos.graphics=compat` path: Xorg modesetting
with glamor/GLX, hardware cursors and page flipping disabled, while retaining
the KMS driver. The normal installer retries that configuration after an Xorg
session failure. Installation-session screen blanking and display power saving
remain disabled. Original-kernel RNG checks passed x86 modern/legacy VirtIO
enrollment with RDRAND disabled, ARM RNDR enrollment, and expected refusal when
no secure entropy source is available.

`SOURCE_COMMIT.txt` records the final source above; `PAYLOAD_COMMIT.txt` records
the base system. Release filenames contain `installer-preview.2`. The two full
ISOs install all bundled components in one operation; manifests, hashes and
`installation-validation.tar.gz` are supporting records, not separate packages.

Desktop applications run on Xfce/Debian/Linux. The original C/assembly kernel
remains an independent experimental entry. The user confirmed that disabling
UTM's Apple Virtualization option allowed the previous ARM64 ISO to reach its
installer on a Mac; that report does not establish completed installation or
Apple Virtualization.framework compatibility. macOS HVF, Apple Virtualization,
physical PCs/boards and universal hardware support are not certified by these
Linux-hosted tests.

## Installer preview.1 — 2026-10-16

[Verification run 38859355432](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38859355432)
passed for both complete installation ISOs. The system payload comes from
`25cccedb6d25c499c677baac4b9ba0b74b41f753`; the installer display overlay and
verification code come from `6426b950217e70f5fdf371114ab202d20605a64b`.
The two image-build steps and original-kernel randomness job succeeded in
[source build 38852455213](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38852455213).
The final images were checked after adding the display overlay, including a
second verification of each ISO's SHA256 after the VM tests.

| Check | amd64 | ARM64 |
| --- | --- | --- |
| ISO GUI, USB keyboard/pointer, account setup and installation to a new 40 GiB disk | Passed, KVM | Passed, TCG |
| Remove ISO, reset UEFI variables, boot installed VirtIO disk | Passed, KVM | Passed, TCG |
| Move the same installed disk to NVMe and boot with fresh UEFI variables | Passed, KVM | Passed, TCG |
| Ordinary-account graphical login, session, window manager and visible top panel | Passed on both disk controllers | Passed on both disk controllers |
| BIOS boot from installed disk to graphical login | Passed, KVM | Not applicable |

These runs used Debian 13's QEMU and firmware, two guest CPUs, 4 GiB guest RAM,
VirtIO graphics and a virtual RNG. Installed-system checks require an ext4 root,
absence of `boot=live`, a normal user ID, the installed-system marker, and the
chosen NET_ADMIN/NET_RAW delegation. Firefox, VS Code, nano and nmap executables
must be present. This run does not claim interactive testing of every bundled
application. The desktop check waits for the panel to actually render; starting
the Xfce processes or displaying only the greeter wallpaper is insufficient.

The ARM installation reached completion after about 17 minutes under software
emulation, with display checks throughout. An additional local test of the same
installer-session change left its GUI untouched for 11 minutes and confirmed
that it stayed visible beyond the previous ten-minute screensaver/DPMS timeout.

The original-kernel job separately passed real account enrollment with x86
RDRAND disabled and each of modern/legacy VirtIO PCI RNG devices, ARM64 RNDR
without firmware/VirtIO entropy, and expected refusal on both architectures
when no secure randomness source exists. No predictable fallback was added.

Release assets include `installation-validation.tar.gz` with the native RNG
logs and both architectures' serial logs, graphics captures and acceleration
records. `SOURCE_COMMIT.txt` identifies the final overlay/test source;
`PAYLOAD_COMMIT.txt` identifies the unchanged base system payload. The desktop
environment is Xfce on Debian/Linux; the C/assembly kernel is an independent,
experimental boot entry.

Physical PCs/boards, macOS HVF, UTM/Parallels/VMware configurations and ARM Secure
Boot were not exercised by this run. QEMU results do not establish universal
hardware support or production security certification. Older checks below
retain their original dates and scope.

### Audit of the public downloads — 2026-10-16 07:35 UTC

Both ISO files were downloaded again from the public `v2.0-installer-preview.1`
release, rather than read from a local build fixture. Their complete SHA256s are:

| Architecture | Bytes | SHA256 |
| --- | ---: | --- |
| amd64 | 2120286208 | `faf363a105531c81ea58893d4fdf24cfae1ce69f2cc02983daeb5bea73430407` |
| arm64 | 2123169792 | `bd59db08807073a8f8f611091d8db5882aba2cb5987149b49c1c1913ba262d2b` |

Extraction of each ISO's actual `/boot/grub/grub.cfg` confirmed that its first
entry is `Install mnOS - ARCH (virtual machines and hardware)`, followed by
software rendering, a temporary desktop, and the experimental kernel. Neither
contains the old three-entry `mnOS Desktop 2.0 - hardware` menu.

Both extracted system filesystems match their internal payload checksums. Their
dpkg databases mark Firefox, VS Code, nano, nmap, Wireshark, GCC, Rust, Go,
Node.js, Xfce, Intel Wi-Fi firmware, QEMU/SPICE agents and VMware tools as
installed. The original kernel inside each installable filesystem is
byte-identical to the corresponding native kernel on the ISO. The backend
deploys that whole filesystem in one operation; it does not fetch components.

Both public downloads were then booted with QEMU 10 / TCG, fresh UEFI variables,
VirtIO graphics, 4 GiB RAM, a new empty 40 GiB disk and **no network adapter**.
Each reached `MNOS INSTALLER GUI READY` and a visible graphical installer:
[ARM64 screenshot](../assets/installer-release-arm64.png),
[amd64 screenshot](../assets/installer-release-amd64.png).
This additional check stopped at the installer and did not write to the target
disk; the complete offline installation and subsequent disk boots are covered
by the successful verification run above. No ISO bytes changed during this audit.

## Historical preview.5 build

On 2026-10-15, [workflow run 38730394222](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38730394222)
successfully built and checked source commit
`2f55f09aa2a9983479fa13fc0e3f3297a4a0a4ab` for all four images:

| Image | Linux ISO boot | Original kernel in the same ISO |
| --- | --- | --- |
| QEMU x86_64 | BIOS, UEFI and Secure Boot passed | BIOS and UEFI passed 8/8 |
| Hardware x86_64 | BIOS, UEFI and Secure Boot passed | BIOS and UEFI passed 8/8 |
| QEMU ARM64 | UEFI passed | Not ported |
| Hardware ARM64 | UEFI passed | Not ported |

The first upload attempt was rejected because GitHub does not accept empty
assets. [Publication run 38735643366](https://github.com/zengyiqian2014-boop/mnOS/actions/runs/38735643366)
rechecked the five successful build/test jobs and source identities, converted
empty device-tree inventories to explicit "no files" reports, and published the
same checked ISO binaries. No ISO rebuild or tag change was involved.

These are actual ISO boots under QEMU, including the hardware-profile images.
They do not establish physical PC/board compatibility or ARM Secure Boot support.
Every image also passed kernel configuration checks, compiled desktop/catalog
checks, **five fresh-container account enrollments**, ordinary/delegated network
permission tests, and ordinary-user C/C++, Rust, Go, Java and JavaScript builds.
The editor checks covered nano and the official VS Code CLI. Graphical VS Code
interaction was separately tested in the x86_64 development VM described below.

The same run passed all original-kernel host and VM tests listed below, including
32 vCPUs, 33 TiB sparse addressing, account permissions, both display adapters
and ATA persistence after power loss. ISO checksums are verified again when
staging release assets. Package manifests, source commit IDs, kernel configs
and boot logs are retained with the build.

## Current desktop interaction checks

On 2026-10-15, the updated C++ binaries and packaged desktop assets were tested
inside an x86_64 QEMU development VM at 1920×1080. This was a development image
with the new components copied in, not a claim about an untested release ISO.

- Ordinary-account login and lock/unlock succeeded.
- The top-left compiled menu showed themed icons and categorized applications.
- Typing `nmap` and immediately pressing Enter opened an actual terminal running
  `/usr/bin/nmap --help`. A delayed search-signal race was fixed and a C++
  self-test now covers filtering before Enter.
- Official VS Code 1.141.2 opened as the ordinary account with Electron sandbox
  enabled. A C++ file was typed into the GUI, saved with Ctrl+S, verified as owned
  by that user, then compiled with g++ and executed as the same user.
- The current VS Code package renamed its desktop file to
  `com.microsoft.VSCode.desktop`; panel, menu favorites and checks now accept
  that ID as well as the older `code.desktop`.
- The compiled enrollment PTY test passed cancellation, invalid account names,
  hidden passwords, password hashing, mode-0600 marker and idempotence. The
  service disables core dumps to reduce accidental password disclosure.
- Network capability tests passed ordinary networking, explicit delegation,
  outsider denial, allowed-tool enforcement and revocation.

The four-image workflow now builds on native x86_64 and ARM64 runners. At commit
`f2676e1`, the x86_64 QEMU image passed Linux BIOS/UEFI/Secure Boot and native
BIOS/UEFI boot checks. Both ARM64 profiles passed installed desktop, enrollment,
network permission and development-tool checks, but their ISO boot checks
caught SquashFS XZ decompression failures. The ARM32 BCJ filter was incompatible
with the ARM64 kernel's enabled decoders; ARM64 packing now uses plain XZ, while
x86_64 keeps its supported x86 BCJ filter. The corrected images passed the
preview.5 boot checks above. ARM QEMU also has explicit USB keyboard
and tablet devices.

Enrollment uses unbuffered terminal input and `posix_spawn` for password and
account helpers. Host checks exercise binary stdin, closed stdin, environment
isolation and spawn errors. Each final image repeats account setup in five fresh
containers, independently of GUI, network and compiler checks.

Physical ARM64 boards and ARM Secure Boot have not been validated; see
`HARDWARE.md`. Workflow artifacts record architecture-specific results. A driver
configuration check is not a hardware compatibility certification.

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
profiles for each architecture from the release source and repeats native tests,
package/GTK checks, x86_64 BIOS/UEFI/Secure Boot Linux boots, ARM64 UEFI Linux
boots, and combined x86_64 ISO native BIOS/UEFI boots before publishing any ISO.
Linux checks use the unmodified release image;
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
