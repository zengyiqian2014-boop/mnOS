# Unified offline installer

Build one installation ISO per CPU architecture:

```sh
./live/build.sh installer amd64   # on an x86_64 builder
./live/build.sh installer arm64   # on an AArch64 builder
```

Each image includes the same desktop, development and security tools, physical
hardware firmware, VirtIO drivers, QEMU/SPICE agents and VMware guest tools.
The ISO starts the mnOS C++/GTK installer. It carries a compressed filesystem
as its offline payload; the installed system runs from the destination disk.
No Python was added to the installer or its new integration test. Third-party
applications may require Python libraries.

Download only the ISO matching your CPU architecture. The desktop, Firefox,
VS Code, development/security tools, drivers and original experimental kernel
are installed together in one offline operation. Checksums, manifests and test
archives on the release page do not need installing. Alternative GRUB entries
are boot modes, not successive component installers.

## Installation

Allocate at least **4 GiB RAM and a 40 GiB virtual disk** for a VM. Choose the ISO
matching the VM architecture. Hardware acceleration requires the host and guest
CPU architectures to match: use ARM64 with Apple Silicon/ARM hosts, amd64 with
Intel/AMD hosts. ARM64 requires 64-bit UEFI. amd64 supports BIOS and UEFI.

1. Select **Install mnOS**. The compatibility entry keeps the display driver
   enabled and explicitly disables Xorg glamor/GLX acceleration, hardware cursors
   and page flipping with `mnos.graphics=compat`. The normal entry also retries
   this configuration if its Xorg session exits with an error. It does not use
   `nomodeset`, which disables VirtIO GPU output on ARM machines without an EFI
   framebuffer.
2. Choose an unmounted, writable disk of at least 32 GiB. Enter a username and
   a strong password of at least 12 characters. Optionally delegate network
   administration capabilities to that account.
3. Review the selected disk and type its full device path to confirm erasure.
   **This version replaces the entire selected disk.** It does not offer
   partition resizing, preservation of an existing OS, or disk encryption.
4. Wait for installation to finish, then shut down and remove the ISO/USB.
   Start from the installed disk and log in using the account you created.

The installer refuses mounted, read-only, undersized and non-disk targets. It
checks the payload checksum before partitioning, writes GPT/ESP/ext4, creates
the account without exposing its password in arguments or logs, regenerates
the hardware-capable initramfs, and installs the disk boot configuration. Root
login stays locked; the account uses sudo for administrative operations.

## Virtual displays

[UTM on Apple Silicon: backend selection and troubleshooting](UTM.md) distinguishes
Apple Virtualization.framework from QEMU's HVF CPU acceleration. Neither macOS
backend has been directly tested here; the automated tests use Linux KVM/TCG.

Use a **VirtIO GPU display** with UTM/QEMU ARM64 and a USB keyboard/pointer.
Start with 3D acceleration disabled. QEMU's “Display output is not active”
message comes from the emulator, not from the installer; it means the selected
virtual display is not currently producing an active output. That message alone
does not distinguish driver initialization, display blanking or an earlier boot
failure. The installer session disables the screensaver and display power saving;
the VM must also have a compatible virtual display configured.

`live/run.sh` selects KVM on a compatible Linux host and HVF on a compatible
macOS host, otherwise TCG. It uses the same installer ISO in either mode. Pass
an existing qcow2 disk with `DISK=/path/to/disk.qcow2`; the launcher never creates
or overwrites a host disk automatically.

## Original kernel

The installer includes the original architecture-specific C/assembly kernel
and adds an experimental entry to the installed GRUB menu. Desktop programs
still use the independent Linux environment. The experimental ARM kernel needs
GOP for its graphics; it also has a PL011 serial console. It does not yet drive
every PCI/USB display/input device supported by Linux. Secure Boot must be off
to start the unsigned original kernel.

Native account creation needs a secure random source. x86 supports RDRAND and
VirtIO PCI RNG (modern/transitional); ARM supports VirtIO MMIO RNG, firmware
entropy and RNDR where available. In QEMU, keep the entropy device enabled.
Without any of these sources the kernel refuses enrollment; it does not invent
randomness from timers or bypass account protection. This restriction belongs
to the experimental kernel, not the Linux installer account setup.

## Recognizing the current installation media

The first menu entry in installer preview.1 is **Install mnOS - arm64 (virtual
machines and hardware)** or its **amd64** equivalent. A three-entry menu reading
`mnOS Desktop 2.0 - hardware`, `safe graphics` and `boot messages` is the older
desktop boot configuration. Shut down, inspect every attached virtual optical
drive, remove older media, and attach just the matching installer ISO. Verify
its SHA256 against the checksum on the installer release page if in doubt.

At a `grub>` prompt, `boot` only starts a kernel that has already been loaded.
`you need to load the kernel first` means none is currently loaded. That message
alone neither identifies the original loading error nor calls for another
kernel installation. Press Esc to return to the menu when available; if
**Install mnOS** fails, record the error immediately after selecting that entry
and the VM's mounted ISO filename and configuration.

## Verification

`tests/install_vm.cpp` boots the actual ISO, enters the account and disk
confirmation through virtual keyboard events, installs to a newly created
qcow2 disk, powers off, and restarts with **no ISO attached and fresh UEFI
variables**. It verifies an active graphical installer, the installed ext4 root,
ordinary-user login, desktop session, and application executables. It repeats
disk boot with VirtIO and NVMe controllers and checks BIOS boot on amd64.

Logs and screenshots are collected with each build. Passing these virtual tests
does not certify untested physical PCs, proprietary GPUs, all ARM boards or
bare-metal Apple Silicon. Model-specific ARM firmware/DT and physical validation
remain separate requirements.
