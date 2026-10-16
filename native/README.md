# mnOS native kernel

An experimental x86_64 and ARM64 kernel written in freestanding C and assembly.
GRUB Multiboot2 enters the x86 kernel; ARM64 supports direct FDT boot and UEFI
with FDT or ACPI. Linux does not run underneath either kernel. See the
[ARM64 build, tests and compatibility limits](arch/arm64/README.md).
The implemented list below describes x86 unless stated otherwise.
The GNU toolchain, GRUB and Noto font are external components. Original code is
GPL-3.0-or-later; the font license is in `assets/FONT-LICENSE.txt`.

## Implemented

- Sparse physical page allocation up to **64 TiB of physical address space**,
  limited further by the processor's address width. High RAM has a separate
  direct map; page tables use 1 GiB pages when available and 2 MiB/4 KiB otherwise.
- Dynamic CPU records discovered through ACPI MADT, including 32-bit x2APIC IDs.
  There is **no fixed CPU-count array cap**. Each online CPU has its own guarded
  stacks, TSS, page tables, ring-3 tasks and preemptive timer. Idle uses HLT.
- Supervisor/user separation, W^X, NX, null/stack guards, CR0 write protection,
  optional SMEP/SMAP and checked system-call pointers. SWAPGS protects per-CPU
  context when a user process clears its GS selector.
- Original [MNFS](FILESYSTEM.md): copy-on-write metadata/data, CRC32C, 64-bit
  sparse files, directories, rename, truncate, collection and one saved snapshot.
  `/ram` is temporary; an optional whole MNFS ATA disk is mounted at `/disk`,
  opened read-only for validation. Authenticated home-directory writes are enabled
  after login; root can explicitly lock the volume. Boot never formats a disk.
- Local account enrollment/login, separate ordinary/root passwords, user home
  permissions, password changes, root account creation and session locking.
- Seven-page graphical UI, English/Chinese console help, PS/2 keyboard/mouse,
  COM1 serial, PCI enumeration and basic ACPI S5 power-off.
- QEMU/Bochs DISPI 2D mode switching, three wallpapers and MNFS-backed display
  preferences. Other firmware framebuffers retain their boot-time display mode.

Memory validation covers allocator boundaries at 64 TiB and an actual QEMU boot
with **33 TiB of sparse guest RAM**, including reads/writes above 32 TiB. This is
addressability testing, not a stress test of 33 TiB of physical RAM. SMP tests
boot 4, 8 and 32 CPUs plus two CPUs with sparse APIC IDs 0 and 256. Dynamic data
structures do not mean literally unlimited CPUs: firmware, APIC IDs, RAM and
tested scale still impose limits.

CPU hotplug, task migration, NUMA placement, native USB/network/Wi-Fi/audio,
AHCI/NVMe, GPU acceleration, a general executable loader and a POSIX/Linux ABI
are not implemented. Tasks are fixed demonstration/test programs per CPU.
Native authentication covers the local graphical/serial console and its file and
settings operations; it is separate from Linux PAM/sudo and is not POSIX UID/ACL
support for arbitrary native programs. Speculation mitigations and a full
security audit remain outstanding. No performance advantage over RHEL/Linux or
security certification is claimed. Firefox, CTF tools and common hardware
drivers run in the separate Linux entry on both release ISOs.

## Help and kernel commands

`help` **defaults to English**. `-cn` selects Chinese explicitly in both the
native console and the Linux terminal:

```text
help
help -cn
kernel help
kernel help -cn
```

Native command syntax is `kernel <instruction> [object]`:

```text
kernel info
kernel cpu
kernel memory
kernel gpu
kernel devices
kernel test
kernel display 1600x900
kernel wallpaper 1
kernel save-settings
```

Display modes: 1024×768, 1280×720, 1600×900, 1920×1080 and 2560×1440, subject to
the supported DISPI adapter and available VRAM.
The settings selector skips modes that exceed VRAM; QEMU VirtIO VGA's 8 MiB
legacy framebuffer does not fit 2560×1440 at 32 bpp. Wallpaper IDs: 0 contours,
1 grid, 2 plain. `kernel save-settings` writes the signed-in user's
`/disk/home/NAME/display.cfg` (root uses `/disk/root/display.cfg`). Ordinary users
can change and save their own preferences. A root-imposed `disk lock` blocks
disk writes until root unlocks it. Preferences load after authentication.

## Accounts and ordinary-user operations

First boot asks for an ordinary username/password and a separate root password,
then opens the ordinary session. There is no default password. Passwords use
PBKDF2-HMAC-SHA256 with 600,000 iterations and a fresh 128-bit random salt.
x86 uses RDRAND or the original VirtIO PCI entropy driver (modern/transitional);
ARM64 uses VirtIO MMIO, a pre-boot UEFI RNG pool, or the optional RNDR instruction.
Creation fails closed if none is available. For QEMU, attach `-device virtio-rng-pci`
on x86; on ARM FDT guests use `-device virtio-rng-device` with modern MMIO enabled.
The ARM UEFI/ACPI path can obtain entropy through the firmware RNG protocol.
No timestamp, fixed seed or predictable fallback is used. Password input is masked,
excluded from history/serial output and cleared from temporary buffers. Failed
authentication imposes a delay; root sessions expire after five minutes.

| Command or operation | Permission |
| --- | --- |
| Queries, display/wallpaper, reboot/shutdown of this local session | Signed-in user |
| Read/write/create/rename/delete files under `/home/NAME/` | That user or root |
| `passwd` | Own account, after current-password verification |
| `su` / `exit` | Authenticate root / return to previous ordinary session |
| `logout` or `lock` | Lock the session; subsequent input requires login |
| `useradd NAME`, `passwd NAME`, volume lock/unlock, snapshot and collection | Root |

Up to 16 native accounts are currently stored. `/root` is private; `/system`
credential data is inaccessible through console file commands, including aliases
and rename destinations. Dot/empty path components are rejected before access
checks. Ring-3 demonstration tasks have no file/account system-call interface.

The credential record resides in the MNFS disk's `/system/accounts.bin`; without
a disk, accounts are temporary and enrollment repeats next boot. A damaged store
locks access instead of enabling new enrollment. Restoring a disk snapshot also
restores its account record and locks the session for reauthentication. This
protects console access, not against someone editing an unencrypted disk or
replacing the unsigned kernel offline. MNFS has no at-rest encryption yet.

File commands: `fs`, `ls [PATH]`, `stat PATH`, `cat PATH [OFFSET]`, `write PATH
TEXT`, `writeat PATH OFFSET TEXT`, `mkdir PATH`, `rm PATH`, `mv FROM TO`,
`truncate PATH SIZE`, `gc [/ram|/disk]`, `disk unlock|lock`, and `snapshot
save|restore|drop [/ram|/disk]`. They also accept the `kernel ` prefix. Unprefixed
paths select `/ram`; include `/disk/` to access the persistent volume. `cat`
displays at most 512 bytes per call. The console accepts at most 255 ASCII bytes
per command; the filesystem API supports 255-byte names and 1024-byte paths.

Other console commands: `uname`, `mem`, `tasks`, `pci`, `test`, `uptime`, `clear`,
`reboot`, `shutdown`. F1–F7 select pages; `t` opens the console. F7 shows display
settings; R/W/S change resolution/wallpaper/save. Outside the console, `p` opens
the power menu; R/S/Escape reboot, shut down or cancel. Serial input uses the
same commands. Use PS/2 input, not a USB tablet.

## Persistent disk example

On the build host (or using the Linux environment's installed `mnfs` tool):

```sh
make -C native all host-tools
native/build/mnfs format native/build/data.img 64
qemu-system-x86_64 -m 512 -smp 4 -cpu max -vga std \
  -cdrom native/build/native-demo.iso -boot order=d \
  -drive file=native/build/data.img,format=raw,if=ide,index=0
```

`format` creates a **new regular file only** and refuses existing paths, symlinks
and device nodes. Native ATA supports legacy PIO ports and 512-byte sectors; use
QEMU's default i440fx/IDE machine for this example. The Linux launcher's q35/
VirtIO disk does not expose this native ATA path. Create an ordinary account
named `demo` at enrollment and sign in, then use the native console:

```text
write /disk/home/demo/note saved-on-disk
writeat /disk/home/demo/large 4294967294 crosses-4-GiB
kernel save-settings
su
snapshot save /disk
disk lock
exit
```

Use a disposable image while testing this early filesystem. Linux accesses MNFS
images through the `mnfs` utility; MNFS is not the Linux root filesystem and is
not APFS-compatible. FAT may still be used for the standard EFI boot partition.

## Build and verification

On an x86_64 host with GCC, binutils, make and Docker:

```sh
make -C native -j4 all host-tools
make -C native host-tests
docker build -t mnos-live-builder -f live/Builder.Dockerfile live
docker run --rm --network none --memory=6g --cpus=4 \
  -v "$PWD/native:/native:ro" -v "$PWD/native/build:/out" \
  mnos-live-builder bash -c '
    set -e
    bash /native/pack.sh test
    python3 /native/tests/boot.py /out/native-test.iso
    python3 /native/tests/smp.py /out/native-test.iso
    python3 /native/tests/large_memory.py /out/native-test.iso
    bash /native/pack.sh demo
    python3 /native/tests/ui.py /out/native-demo.iso
    python3 /native/tests/ui.py /out/native-demo.iso --gpu virtio --uefi
    python3 /native/tests/persistence.py /out/native-demo.iso
  '
```

Host tests run ASan/UBSan against allocation, large files, B-tree splits,
snapshots, ENOSPC rollback, damaged data/metadata, torn writes and reordered
write caches. VM tests require per-CPU `SELFTEST PASS 8/8`, the expected online
count and debug-exit 33, or explicit UI/filesystem interaction results. They also
verify ATA writes across a VM power cut, saved display preferences and host
remount. See [validation record](../live/VALIDATION.md) for coverage and limits.

The standalone demo remains interactive after checks. Full dual-boot ISOs use
`./live/build.sh qemu` or `./live/build.sh hardware`. Secure Boot must be off for
the unsigned native kernel; the separate Linux entry retains Debian's signed
boot chain. The source and font are included in `/usr/src/mnos-native` on both
full ISOs. Normal builds use the checked-in font atlas. After editing UI text:

```sh
python3 native/tools/font.py /path/NotoSansCJK-Regular.ttc native/assets/font.c
```

## System-call ABI

`int 0x80`: RAX operation, RDI/RSI arguments, RAX result; other registers are
preserved. Calls: 0 bounded console write (length ≤256), 1 yield, 2 local CPU
ticks, 3 exit, 4 boot-probe report (test task only), 5 sleep (1–1000 ticks).
Bad addresses return -14; invalid arguments -22; unknown calls -38. Filesystem
operations currently run in the kernel console, not through a ring-3 file ABI.
