# Linux guest on the original mnOS kernel

The host remains the original C/assembly mnOS kernel. Its own RV64GC/Sv39
interpreter runs Linux 6.12 and Debian riscv64; neither QEMU nor a hidden Linux
host executes guest instructions in the installed system. QEMU and libslirp
are used only by build/test tools. This is software interpretation, not hardware
virtualization. See [edition boundaries](../../EDITIONS.md).

The guest includes APT, glibc, a dynamic linker, Xorg/fbdev, Xterm, twm,
the C++ mnOS application center, Firefox ESR, Mousepad, Thunar, the C++ toolchain,
standard file/process/network commands and nano. Software must have a riscv64
or architecture-independent build. An AMD64 or ARM64 DEB is not translated into
another architecture by unpacking it. Official VS Code AMD64/ARM64 packages
therefore remain a Linux Edition feature; arbitrary applications and 3D/GPU
acceleration are not promised.

## Build

Use the dependencies in `native/Builder.Dockerfile`. Inside a disposable root
builder with a registered `qemu-riscv64` binfmt handler and permission to mount
`/proc`:

```sh
cd native
guest/build.sh
bash guest/build-debian.sh
make host-tests
GUEST=1 installer/build.sh
```

`build.sh` pins the Linux, BusyBox and musl source archives by SHA256. The tiny
bootstrap is static LP64/musl. The mounted Debian filesystem uses the normal
LP64D/glibc ABI; the original interpreter implements F and D without host floating
point. Debian packages are authenticated by Debian's archive keys. Package
versions are recorded in `build/guest/DEBIAN-PACKAGES.tsv`; builds use the current
Debian 13 repositories and are not bit-for-bit reproducible across repository
updates. mnOS programs use C/C++/assembly, not Python or Java. GTK build tools
and their unused dependencies are removed after compilation; only dependencies
needed by the selected runtime packages remain. The package manifest records them.

`prepare-debian.sh` produces `rootfs.squashfs`. The native installer embeds this
file together with Linux, so booting the guest and opening its included desktop
does not require a download. Installing additional packages with APT requires
network access. An integrated installer is larger than the separate USB driver
preview; compare filenames, checksums and package manifests, not size alone.

Before ISO packaging, `verify-image.sh` extracts the guest's init, desktop
session, launchers and corresponding application sources. It compares them
with the current build inputs, checks the executable architecture and validates
the guest kernel configuration and hashes. A stale staging directory fails
the build instead of producing an ISO with an older initialization script.

The Debian builder precomputes Fontconfig's le64 caches after rounding font
directory timestamps to SquashFS's second precision. This avoids invalidating
the caches and rescanning all fonts during the first interpreted X11 launch.
It also generates the MIME cache needed by GTK's embedded icons. The guest
kernel enables shared memory and memfd; init mounts `/dev/shm` as a separate
tmpfs with mode 1777, `nosuid` and `nodev`. Pages are allocated on demand, with
the kernel's default size limit proportional to the guest's RAM.

## Use and persistence

On the host, `linux open applications` asks to enable simulation, then starts the
application center in X11. `linux open desktop` shows the graphical terminal.
`linux start` opens the line console. F5 returns to the host. In the Linux window:

```sh
apt update
apt install x11-utils
apt list --installed
mnos-gui list
mnos-gui open firefox-esr.desktop
mnos-gui run mousepad /root/example.txt
# In Xterm, applications open real windows:
xmessage 'Hello from Debian'
nano /root/example.txt
poweroff
```

A cold X11/GTK start on an outer software VM can take many minutes. The launcher
keeps the request pending for up to 30 minutes while its session supervisor is
alive, and reports an early exit immediately. Startup details are in
`/tmp/mnos-gui.log` and `/run/mnos-gui/session.log`; failures also appear in the
Linux terminal. This is a bounded compatibility path, not a claim of interactive
performance on every host.

The application center discovers visible installed `.desktop` entries through
GIO, refreshes on installation, and searches names, IDs and descriptions. Its
Install action opens an APT terminal through `mnos-pkg`. Programs without desktop
entries can use `mnos-gui run PROGRAM [ARG...]`; argument splitting never invokes
a shell. `show`, `desktop`, `hide` and `status` reuse one graphical session.
Requests use a private Unix socket with peer-UID checks and bounded async reads.
Closing the center hides its window, leaving other applications running.
If a launched child exits with an error or signal, the center reopens with the
failure and records it in `/run/mnos-gui/session.log`.

`mnos-pkg install` asks an additional default-no confirmation for explicitly named
Python packages and `linux-libc-dev`. APT still shows dependencies and its own
confirmation. Header files consume disk space; they are not runtime libraries
and do not themselves cause memory leaks. Direct `apt` retains upstream behavior.

Use `help` / `help -cn` in either environment. Guest root has no host account
privileges. The host's authenticated user owns a private sparse 8 GiB
`linux-data.img` in their MNFS home directory. Linux mounts the bundled SquashFS
read-only and an ext4 upper layer from this file. Packages and files survive a
clean shutdown. A run without an installed disk uses temporary storage. A locked
or invalid existing data disk prevents startup instead of silently replacing it.

Power off inside Linux before `linux stop`, logging out or powering off the host.
Forced stop is equivalent to a power failure: unflushed work can be lost. Stopping
wipes RAM, not the persistent data file. Closing the window leaves jobs running.
The guest defaults off after every host login. `linux pause` freezes execution;
`linux resume` continues it. Ordinary idle waits yield the CPU without stopping
scheduled jobs. Time follows the host's monotonic timer, not instruction count.

RAM defaults to half the available host memory, up to 1 GiB. `linux memory MIB`
changes the limit while stopped (128 MiB..512 GiB, bounded by available host RAM).
The host reserves 32 MiB and allocates private pages on demand. For APT/X11, use
at least 768 MiB guest RAM and a 4 GiB host. A larger limit is not extra physical
RAM and does not add CPUs. There is one interpreted CPU.

## Device boundary

`guest_devices.c` validates guest physical buffers, sizes and disk offsets.
The host advertises a 128 KiB maximum disk request. The Linux driver gathers
each bio into a bounded contiguous buffer, so one host transaction persists the
request instead of committing every 4 KiB page separately. Older hosts retain
the 4 KiB fallback. This is synchronous batching: a successful write still
finishes the MNFS transaction before returning, and PREFLUSH/FUA are preserved.
Supervisor-only SBI extension `0x09004d4e` provides:

- Read-only bundled SquashFS and the private writable data file, never a raw host disk.
- Ethernet frames through the original VirtIO 1.0 PCI/MMIO driver. Linux handles
  TCP/IP and obtains its address through DHCP. Only one guest owns this NIC.
- Keyboard and absolute pointer events, host CSPRNG entropy and initial time.

Xorg renders to a reserved 1024x768 XRGB framebuffer that the original GUI reads.
There is no host filesystem passthrough or accelerated 3D. Physical Wi-Fi,
non-VirtIO NICs and universal hardware compatibility are not implemented here.
The interpreter is a bounded kernel component, not yet a separately isolated
native user process; this is not a security certification.

## Validation

`make host-tests` covers integer-only floating point, ISA/MMU/privilege checks,
SBI buffer boundaries, disk bounds, consent, session changes and RAM wiping under
ASan/UBSan. `tools/guest-desktop.c` runs the same interpreter/device implementation
on a development host, using libslirp only as the test NIC. It supports
`MNOS_GUEST_COMMANDS`, `MNOS_GUEST_INPUT` and `MNOS_GUEST_EVENTS` FIFOs.

Hosted execution is not evidence of a native boot. The release gate is
`MNOS_TEST_LINUX_GUEST=1 build/desktop-vm build/desktop amd64` and `arm64`:
install to a new disk, remove the ISO, boot the original kernel, authenticate,
write a 1 MiB binary file and verify its checksum again after guest poweroff,
run signed APT update/install over native VirtIO, open X11, deliver real input,
discover installed apps, open the real GTK center and Mousepad, and check pixels
painted by JavaScript in a real Firefox window,
shut down and verify package/file persistence. `MNOS_TEST_PROXY` can set a test
network proxy; no development proxy is baked into the image.

The native-boot test checks the bundled GUI before downloading packages. Its two
APT steps each have a one-hour ceiling and report elapsed time: parsing the full
Debian catalog under an outer QEMU software CPU can take billions of interpreted
instructions. AMD64 and ARM64 run in separate CI jobs on matching host
architectures; the release gate requires both, their matching source commit and
identical guest package versions. GUI startup/window waits are bounded at 30
minutes (one hour for the browser). A single C++ `mnos-window-wait` process
keeps an X11 connection open and checks mapped window titles, including
reparented windows. It avoids repeatedly executing and dynamically linking
`xdotool` while the application starts. Timeout diagnostics include the GUI
session log; CI also retains the last captured framebuffer after a failure.
When `/dev/kvm` is available on a matching
architecture, the **test host** can use it;
`MNOS_TEST_TCG=1` forces TCG. This does not add hardware acceleration to the
installed RV64 guest. The tests also boot without CPU/VirtIO random devices to
exercise the UEFI TPM entropy fallback and refusal when all trusted sources are absent.

`bash native/tests/gui_apps.sh` separately tests the GTK service and CLI under
Xvfb: window-wait timeouts, hidden/renamed/reparented windows, desktop discovery, hidden entries, literal arguments and a
silent client's inability to block the UI. These hosted checks complement the
native boot tests; they do not replace them. Firefox runs with its standard
sandbox; the guest kernel enables user namespaces and seccomp for that purpose.

## Sources and licenses

Linux and `mnos_devices.c` use GPL-2.0-only; BusyBox uses GPL-2.0-only; musl uses
MIT. Native interpreter/device/UI code uses GPL-3.0-or-later. Debian components
retain `/usr/share/doc/*/copyright`. Preserve source hashes, kernel/BusyBox
configs, package versions and complete corresponding upstream sources when
publishing binary releases. The Debian rootfs is upstream software, not a claim
that the Linux kernel or desktop programs were authored by mnOS.

`linux stats` reports the current guest's retired instructions, interpreter time,
private-disk I/O time, calls, transferred bytes and errors. Times have millisecond
resolution; disk time is included in interpreter time. Counters reset when the
guest stops and are available only to its signed-in host owner.
