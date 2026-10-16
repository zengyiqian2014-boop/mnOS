# mnOS offline installation ISO

Both architecture-specific installers provide two independent environments: the original freestanding
[mnOS native kernel](../native/README.md), and a usable desktop built from
maintained Debian 13 packages (Linux 6.12 LTS, Xfce, Firefox ESR, system management,
CTF utilities). The native kernel has its own framebuffer UI,
preemptive scheduler and ring-3 isolation; it does not run Linux applications.
The original BusyBox build remains available from the repository root.

Use the `installer` profile for both `amd64` and `arm64`:

| Profile | Purpose | Extras |
| --- | --- | --- |
| `installer` | Virtual machines and supported physical hardware | VirtIO, QEMU/SPICE/VMware agents, common GPU/network/Wi-Fi/audio firmware, Bluetooth; x86 CPU microcode |

ARM64 targets QEMU `virt` and generic UEFI hardware; it includes the original
ARM64 kernel as an experimental boot entry. See [INSTALLER.md](INSTALLER.md)
for the C++/GTK installation flow, then [HARDWARE.md](HARDWARE.md) for the two-image matrix, ARM board boot requirements,
driver coverage and the distinction between included drivers and tested devices.

See [DESIGN.md](DESIGN.md) for kernel choices, RHEL design references, tuning,
security boundaries and hardware limitations. There is no default account password.
Installation creates an ordinary local account authorized for sudo; root login remains locked.
The installer erases the whole disk explicitly selected and confirmed by the user.

## Help and administration

**`help` is English by default; `help -cn` selects Chinese.** This applies to the
native console and the Linux terminal. In Bash, `help cd` and other built-in
command references remain available. The command guide is also available as
`mnos-help`, including in non-interactive scripts.

```sh
help
help -cn
kernel help
kernel help -cn
kernel info
kernel memory
kernel cpu
kernel gpu
kernel modules
kernel sysctl vm.swappiness
sudo kernel performance balanced
```

`kernel <instruction> [object]` separates ordinary queries from administration:

| Instructions | Permissions |
| --- | --- |
| `help`, `info`, `memory`, `cpu`, `modules`, `devices`, `gpu` | Ordinary user |
| `irq`, `log`, `sysctl KEY` | Current user's Linux access rights; protected data may require sudo |
| `load`, `unload`, `set`, `cpu-online`, `cpu-offline`, `performance` | Root through an authorized sudo session |

The compiled `/usr/bin/kernel` handles queries and forwards mutations to
`/usr/sbin/mnos-kernel-admin`. The backend independently requires real and
effective UID 0, including when called directly. Both executables are owned by
root, mode 0755, without setuid or file capabilities. Their directory locations
describe their purpose; credentials and Linux access checks enforce permissions.
They use fixed executable paths without shell expansion or automatic elevation.
Runtime sysctl/CPU changes reset at reboot; TuneD profile selection persists.
The original native console has separate account authentication and home-directory
permissions; its [command reference](../native/README.md) describes the scope.

### Network permission choice

The mnOS installer offers a **Network permissions** checkbox with account creation. The
default keeps privileged network operations behind sudo. The optional mode adds
only the created account to `mnos-netadmin` and enables `CAP_NET_ADMIN` plus
`CAP_NET_RAW` on a root-owned, group-executable helper. Run `mnos-net nmap ...`,
`mnos-net tcpdump ...`, or `mnos-net ip|tc|iw ...`; the process keeps the user's
UID. No setuid-root bit, broad sudo exemption or unrestricted shell is supplied.
The capabilities also permit network configuration and raw packet operations;
they are broader than read-only scanning and may be inherited by tool subprocesses.

Ordinary HTTP requests, TCP-connect scans (`nmap -sT`), file editing and desktop
settings need neither this delegation nor root. Wireshark's GUI continues to run
as an ordinary user; open captures made with tcpdump. After installation, root
can authorize another account with `mnos-network-policy netadmin USER` or disable
delegation with `mnos-network-policy root`. A new login refreshes group membership;
already running privileged network tools must exit to lose inherited capabilities.
The initial Live session retains the default policy until explicitly changed.

### Editors and programming

The graphical desktop includes official Microsoft **Visual Studio Code**, nano,
Mousepad and Thunar. VS Code is installed from Microsoft's signed APT repository;
its key is scoped to that source and checked against fingerprint
`BC528686B50D79E339D3721CEB3E94ADBE1229CF`. Versions are recorded in each ISO's
package manifest. VS Code and its terminal run as the ordinary user, with no
`--no-sandbox` flag. Telemetry and AI features default off in new user settings.

Included languages: C/C++ (GCC and Clang), Node.js/npm, Go,
Rust/Cargo and Java/JDK. CMake, Ninja, pkg-config, Git, GDB, clangd, ripgrep and
ShellCheck are available. Build/edit projects in your home directory without
sudo; use project-local dependencies. VS Code extensions are
installed per user. These applications run in the Linux entry, not directly on
the experimental native kernel.

The system center's System settings page opens real Xfce display, wallpaper, keyboard,
mouse, sound and session settings. Mesa OpenGL/Vulkan packages and diagnostic
tools are included in both profiles. `kernel gpu` reports the bound driver;
use `glxinfo -B` / `vulkaninfo --summary` to inspect the active renderer. Installed
drivers alone do not establish hardware acceleration. Python is retained for
Pwntools, mitmproxy, system tools and other dependencies.

The native kernel now includes dynamic SMP, up to 64 TiB physical address
management and [MNFS](../native/FILESYSTEM.md), an original experimental CoW
filesystem. Native display settings can persist to an attached MNFS ATA image.
Linux uses mature Linux filesystems; its `mnfs` utility accesses MNFS image
files, and `mnfs format` refuses to overwrite existing files or device nodes.

## Build

Requires GCC, binutils, make, Docker with BuildKit, a build host matching the target
architecture (x86_64 or AArch64), network access, 4 CPU threads,
at least 8 GiB of host RAM and approximately 25 GiB of free space when using a
normal overlay Docker storage driver. VFS needs more space for full layer copies.
Package downloads use signed Debian repositories. Image tools run in containers;
the scripts do not install packages or start OS services on the host.

```sh
./live/build.sh installer amd64
# On an ARM64 host:
./live/build.sh installer arm64
```

Output goes to `live/out/`. Each ISO has its own SHA256 file, package/source
version manifest and resolved kernel configuration. `OUT=/path` changes the
output directory. A normal build does not need additional CA certificates.
Release uploads split only images exceeding GitHub's 2 GiB per-file limit;
the workflow artifact and local build retain the complete ISO. Download all
`.iso.partNN` files for one image and follow `READ-SPLIT-ISOS.txt` to reconstruct
and verify it before booting or writing to USB. Smaller images stay as `.iso`.
If a build network requires a private CA, set `MNOS_BUILD_CA_BUNDLE` to a PEM
bundle containing its CA and the normal public roots. BuildKit mounts this bundle
only during dependency downloads; it is never copied into an image layer.

APT security updates and dependency resolution can change later builds. This is
a version-recorded build recipe, not a claim of bit-for-bit reproducibility.
The base container digest and mitmproxy version are pinned; manifests record the
actual package versions and Python wheel hashes used for each image.

## Boot and install

```sh
./live/run.sh                              # 4 GiB RAM, two vCPUs
UEFI=1 ./live/run.sh                       # OVMF required
MEMORY=8192 CPUS=4 ./live/run.sh /path/to/image.iso
qemu-img create -f qcow2 mnos.qcow2 40G
DISK="$PWD/mnos.qcow2" ./live/run.sh        # existing disk only
GPU=virgl ./live/run.sh                    # optional host OpenGL + QEMU GTK support
```

KVM on Linux and HVF on macOS are selected when host and guest architectures
match; otherwise TCG is used for functional testing.
The default VirtIO display uses software rendering. `GPU=virgl` requests VirGL;
it requires a working host OpenGL stack and a QEMU build with GTK/OpenGL. This
optional accelerated host path has not been validated in the headless build
environment. The x86 native entry uses the boot framebuffer/DISPI 2D interface;
the ARM native entry requires GOP or its serial console.
The x86_64 ISOs have BIOS and UEFI paths; ARM64 uses UEFI. All have an appended
EFI partition for USB booting on suitable firmware. The Linux signed EFI chain
uses Debian shim/GRUB/kernel, not mnOS-owned keys.
The unsigned **native kernel requires Secure Boot to be off**. The Linux
installation environment is the default ISO entry; select the experimental
native entry to explore its UI and isolation tests. Installed systems on both
architectures retain the original kernel entry, with Linux first.
See [HARDWARE.md](HARDWARE.md) before using an SBC.

To use an ISO on a physical PC, write it to a spare USB drive with an image
writer, selecting the correct target. The image writer erases that USB drive.
Boot the USB and choose **Install mnOS**. The graphical installer asks for a
disk and account, then requires the full disk path to confirm whole-disk erasure.

Live changes reside in RAM and disappear at shutdown. The installer creates a
separate persistent system and a new account from the original SquashFS. Back up
any existing data before changing partitions. No physical machine is implicitly
considered tested merely because its driver is present.

## Checks

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Werror live/tests/install_vm.cpp -o live/out/install-vm
docker run --rm --network none --memory=7g --cpus=4 -v "$PWD/live/out:/out" \
    mnos-live-builder /out/install-vm amd64 \
    /out/mnOS-2.0-preview-installer-amd64.iso /out/validation
```

This test creates its own blank qcow2 disk, uses the actual graphical installer,
and boots the installed disk without the ISO. Use `arm64` for both architecture
arguments on an ARM64 builder. The native entropy regression is C++ as well;
no Python implementation or new Python test is required by this workflow.

In a booted guest:

```sh
sudo mnos-security-check
systemctl --failed
tuned-adm active
nmcli device
hashcat -I
python3 -c 'from pwn import context; print(context.arch)'
```

Tests must include actual BIOS and UEFI boots, first-user enrollment and graphical
login. A GUI constructor test is useful but does not replace boot/interaction
tests. Hardware driver coverage and actual hardware validation are distinct.

## Sources and licensing

Original integration code is GPL-3.0-or-later. The system includes third-party
packages with their own licenses; each package retains its
`/usr/share/doc/<package>/copyright`. `packages-<profile>-<arch>.tsv` records source package
names and versions. Enable matching Debian `deb-src` repositories to retrieve
corresponding sources with `apt-get source source-package=source-version`;
historical versions can be located at <https://snapshot.debian.org/>.
The Linux kernel source is the Debian `linux` source package, including Debian
patches. mitmproxy wheels and resolved versions are listed under
`/usr/share/doc/mnos/`; its sources are available from PyPI and the upstream project.
Microsoft Visual Studio Code is installed from Microsoft's signed APT repository
and retains its own distribution terms and bundled license notices. It is a
third-party binary, distinct from the project's original GPL integration code.
Subscription documents and build credentials are never part of the distribution.

### Compiled mnOS components and provenance

The system center, compact search menu, first-boot enrollment, wallpaper setup
and network policy are built from C++17. The native kernel and privileged gates
are C/assembly. Python development headers, pytest, global pip and pipx are no
longer requested by the default package lists; Python remains only where selected
third-party applications require it. See [implementation origins](PROVENANCE.md)
for the boundary between original code and the Linux compatibility environment.
The menu uses 20 px icons beside a 34 px panel, with icon-only task buttons.
