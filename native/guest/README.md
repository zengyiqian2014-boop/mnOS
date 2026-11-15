# Linux guest on the original mnOS kernel

The host remains the original C/assembly mnOS kernel. Its own RV64GC/Sv39
interpreter runs Linux 6.12 and Debian riscv64; neither QEMU nor a hidden Linux
host executes guest instructions in the installed system. QEMU and libslirp
are used only by build/test tools. This is software interpretation, not hardware
virtualization. See [edition boundaries](../../EDITIONS.md).

The guest includes APT, glibc, a dynamic linker, Xorg/fbdev, Xterm, twm,
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
updates. No Python or Java is authored or installed in this base guest.

`prepare-debian.sh` produces `rootfs.squashfs`. The native installer embeds this
file together with Linux, so booting the guest and opening its included desktop
does not require a download. Installing additional packages with APT requires
network access. An integrated installer is larger than the separate USB driver
preview; compare filenames, checksums and package manifests, not size alone.

The Debian builder precomputes Fontconfig's le64 caches after rounding font
directory timestamps to SquashFS's second precision. This avoids invalidating
the caches and rescanning all fonts during the first interpreted X11 launch.

## Use and persistence

On the host, `linux open desktop` asks to enable simulation, then starts X11.
`linux start` opens the line console. F5 returns to the host. In the Linux window:

```sh
apt update
apt install x11-utils
apt list --installed
# In Xterm, applications open real windows:
xmessage 'Hello from Debian'
nano /root/example.txt
poweroff
```

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
run signed APT update/install over native VirtIO, open X11, deliver real input,
shut down and verify package/file persistence. `MNOS_TEST_PROXY` can set a test
network proxy; no development proxy is baked into the image.

The native-boot test checks the bundled GUI before downloading packages. Its two
APT steps each have a one-hour ceiling and report elapsed time: parsing the full
Debian catalog under an outer QEMU software CPU can take billions of interpreted
instructions. When `/dev/kvm` is available, the AMD64 **test host** can use it;
`MNOS_TEST_TCG=1` forces TCG. This does not add hardware acceleration to the
installed RV64 guest. The tests also boot without CPU/VirtIO random devices to
exercise the UEFI TPM entropy fallback and refusal when all trusted sources are absent.

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
