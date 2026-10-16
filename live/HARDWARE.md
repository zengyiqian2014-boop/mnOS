# Architectures and hardware support

Current releases provide two offline installation images. Each is compiled on a
runner of the matching architecture, with the same mnOS C/C++ tools and catalog.
Virtual-machine support and common physical-hardware firmware are combined.

| Image | Boot target | Original kernel entry |
| --- | --- | --- |
| `installer-amd64.iso` | x86_64 VM or PC, BIOS/UEFI | Original x86_64 kernel |
| `installer-arm64.iso` | AArch64 VM or UEFI machine with Linux-supported firmware | Original ARM64 kernel |

Installation and applications run on the signed Debian Linux kernel. The
architecture-matched original kernel is a separate experimental entry requiring
Secure Boot to be off. The new installer is tested through installation and
no-ISO disk boot using UEFI, plus amd64 BIOS. Secure Boot results from older live
releases do not certify the new installer or ARM64 Secure Boot.

## Driver coverage

Both architectures use Debian's broad kernel configuration rather than a
QEMU-only kernel. Both include VirtIO storage/network/display/RNG, NVMe, USB
storage and input, standard Ethernet drivers and Mesa rendering libraries.
Both installer images include Wi-Fi/Bluetooth/audio/GPU firmware: Intel, AMD,
NVIDIA open drivers, Realtek, Atheros/Qualcomm, Broadcom, MediaTek, TI and others
listed in `packages/hardware.txt`. x86 alone adds Intel/AMD CPU microcode; ARM64
adds Qualcomm SoC and Samsung firmware. Availability still depends on the exact
chip revision and the firmware's licensing/distribution terms.

ARM64 kernel checks also require ACPI/EFI/PSCI, PL011, Rockchip, BCM2835 and
Qualcomm platform support, plus Panfrost/Lima. This checks that drivers were
built; it does not certify any particular board or GPU. Proprietary NVIDIA
drivers, Apple Silicon support and vendor-specific out-of-tree Wi-Fi modules
are not installed by this build.

## ARM board families

| Family | Boot requirements | Current status |
| --- | --- | --- |
| QEMU `virt` | AAVMF, VirtIO | Automated ISO boot test |
| ARM servers/workstations | 64-bit UEFI, suitable ACPI or firmware-supplied device tree | Generic image; physical validation pending |
| Raspberry Pi 3/4/5 | Model-specific firmware, device tree, storage/display support; a suitable UEFI port for this ISO | No board-specific SD image validated |
| Rockchip RK3399/RK356x/RK3588 | Board-specific SPL/U-Boot or UEFI, device tree, power and storage drivers | Platform drivers included; board boot validation pending |
| Qualcomm ARM laptops/boards | Model-specific UEFI/DT, firmware and kernel support | Firmware included; models not yet validated |
| Allwinner/Amlogic/other SBCs | Board-specific boot firmware and device tree | Requires a separate board integration |
| Apple Silicon | Apple's boot chain and an Asahi-compatible platform stack | Not supported by the generic ISO |

Do not write the generic ISO over a board's boot firmware and expect it to
replace SPL/U-Boot. An SBC image needs the exact board revision, bootloader
offsets, device tree, kernel configuration, firmware and a recovery procedure.
No generic raw-disk writer is supplied that guesses these values. Device trees
present in each rootfs are listed in the `device-trees-*.txt` release manifest.

## Run ARM64 QEMU

On Apple Silicon Macs, see the [UTM backend and display guide](UTM.md). UTM's
Apple Virtualization.framework option is distinct from QEMU/HVF acceleration.
Neither has been exercised by the Linux-hosted automated VM tests.

Install `qemu-system-arm` and `qemu-efi-aarch64` on a Debian/Ubuntu host, then:

```sh
ARCH=arm64 DISK=/path/to/existing.qcow2 ./live/run.sh live/out/mnOS-2.0-preview-installer-arm64.iso
```

Use `AAVMF_CODE` and `AAVMF_VARS` to select other firmware paths. Each run copies
the variables template to a temporary file. KVM is used only when the host and
guest architectures match; cross-architecture runs use TCG. `DISK` optionally
names an **existing** qcow2 file. The script does not create or overwrite disks.

Build on a matching ARM64 runner with `./live/build.sh installer arm64`.
Builds fail rather than silently substituting
an x86 package or an unbootable native-kernel menu entry.
