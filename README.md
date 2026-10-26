# mnOS

[English](README.md) | [中文](README.zh.md)

**Current development: two distinct editions.** [Native Edition and Linux Edition](EDITIONS.md)
have separate installers and honest runtime labels. The native GUI now has an
original C software machine for a real Linux/BusyBox guest; Linux Edition keeps
Firefox, VS Code, the full CTF suite and a unified C system-management command.
The guest initially supports RV64IMAC/LP64 console programs, not Linux GUI apps.
See the edition guide for commands, permissions, build steps and limits.

The links and description below document earlier published releases.

[Native graphical desktop installer: AMD64 / ARM64](https://github.com/zengyiqian2014-boop/mnOS/releases/tag/v0.4-desktop-preview.2)
· [Install, use and reproduce the tests](native/DESKTOP.md)

**mnOS Desktop 0.4 installs and boots the original C/assembly mnOS kernel and its
native GUI.** One ISO per architecture includes the graphical installer, boot
loader, desktop windows, application menu, file browser, text editor, terminal,
system monitor, settings and local accounts. Files and preferences persist on
MNFS after removing the ISO and rebooting. This image has no Linux/Xfce/Python
runtime. Press F1–F7 for apps; Ctrl-S saves a document.

The original desktop is a development preview: built-in desktop services still
run in kernel mode. Linux binary compatibility, Firefox, VS Code, native network,
Wi-Fi, USB, AHCI/NVMe, audio, accelerated GPU and APFS support remain unfinished.
Tested hardware models and remaining limits are listed in the installation guide.

The [integrated tools installer](https://github.com/zengyiqian2014-boop/mnOS/releases/tag/v2.0-installer-preview.4)
includes Firefox ESR, official VS Code, nano, C/C++ build tools and headers,
CTF/system utilities and both boot entries in one ISO per architecture. Those
applications run in the Debian/Linux/Xfce environment; the original kernel has
no Linux application ABI. The installer creates an ext4 system; persistent
native files additionally require a supported MNFS volume. Earlier native 0.3
releases were kernel development media; 0.4 adds a native disk installation.

Native Preview 2 adds proportional UI text, original application icons,
49,776 Unicode scalars and 42 documented command entries with Tab completion.
The tools environment provides compiled C `mnos-help` and `mnos-pkg` commands:
`help code`, `help -cn packages` and `help build` explain options, permissions,
examples and troubleshooting. `help TOOL` opens an offline manual or the tool's
catalogued help. Package modifications require explicit `sudo mnos-pkg`.

Native programs support exec, concurrent isolated address spaces, sleep/wakeup,
process ownership and checked MNFS file operations. Applications currently share
the boot CPU and use a small static executable ABI. [Native program reference](native/README.md#run-native-programs-from-mnfs).

The x86 native kernel has passed 32-vCPU and 33-TiB sparse-memory QEMU tests.
The ARM64 port has passed 1/2/4/8-CPU, GICv2/v3, direct, UEFI/FDT and UEFI/ACPI
boots, including an 8-GiB guest with reads/writes above 4 GiB. It is
experimental: these are addressability and functional checks, not unlimited
hardware support, production storage guarantees or performance certification.
See [validation](live/VALIDATION.md), [MNFS format](native/FILESYSTEM.md) and
[design notes](live/DESIGN.md) for the exact scope.

**Help defaults to English.** Use `help -cn` for Chinese. Both environments support
`kernel help` and `kernel help -cn`. Linux queries use your normal account;
changes to system state require an authorized administrator:

```sh
help                         # English
help -cn                     # Chinese
kernel help                  # English kernel command reference
kernel help -cn               # Chinese kernel command reference
kernel gpu                   # Linux: inspect the bound graphics driver
sudo kernel performance balanced  # Linux: change the TuneD profile
```

Native F7 opens resolution/wallpaper settings; an MNFS disk can retain them
across boots. Linux exposes display, wallpaper, audio, network and account
settings through the system center. Python remains included for tools that need
it. Linux installation creates a normal local account with sudo authorization;
root login is locked and there is no default password. `/usr/bin/kernel` handles
queries; `/usr/sbin/mnos-kernel-admin` checks root credentials for mutations.
Restricted logs/parameters still obey Linux permissions. Native accounts are
separate: create user/root passwords on first boot, use `su` for volume/account
administration, and edit your home files or display settings as an ordinary user.
An attached MNFS disk persists native accounts; without one they last one boot.

The installer lets you require sudo for privileged network tools (default), or
authorize the installed account for `mnos-net` with `CAP_NET_ADMIN` and
`CAP_NET_RAW`. For example, `mnos-net nmap ...` runs with network capabilities,
not UID 0. Other accounts are not automatically authorized. Ordinary networking,
TCP-connect scans, file editing, VS Code and project builds need no root.

Native kernel running directly under QEMU:

![Original mnOS kernel](assets/native-kernel.png)

Native display settings, captured under QEMU/OVMF:

![Native display settings](assets/native-settings.png)

Separate Linux desktop and system center, captured in a development VM:

![mnOS Linux desktop](assets/linux-desktop.png)

[VS Code editing and saving a C file as an ordinary user](assets/linux-vscode.png).

The top-left **Applications & Tools** button searches installed apps and 133 named tool entries plus an automatically discovered inventory of installed Linux commands. CLI entries open terminal help; kernel, display, wallpaper and partition controls share **System settings**. The desktop includes a new landscape wallpaper and outline-rendered text, a 34 px panel and 20 px menu icons.

In the integrated Linux tools environment, use your ordinary account to launch
applications and read help:

```sh
firefox-esr
code .
help                       # English by default
help -cn code              # Chinese VS Code instructions
help -cn all | less        # All mnOS chapters; q exits the pager
help nmap                  # Installed upstream manual or built-in tool help
kernel help
mnos-pkg search ripgrep
```

To install additional packages, refresh the signed repository indexes with
`sudo mnos-pkg update`, then run `sudo mnos-pkg install PACKAGE`. Dependency
resolution and the package manager's confirmation remain enabled. Internet
access is needed unless the packages and all dependencies are available locally.
These Linux applications do not run in the original kernel entry.

The graphical tool center now includes **CTF Workbench** and **Linux Command Library**. Open `mnos-center --ctf` or `mnos-center --commands`; installed executable names are indexed without running them. GHex, DB Browser for SQLite and Wireshark open as GUI applications. `help ctf` and `help commands` document the included tool families and offline exercises.

CTF Workbench on the installed AMD64 system, captured after booting from disk
without the ISO and editing a C file in VS Code:

![CTF Workbench on the installed mnOS tools desktop](assets/ctf-workbench.png)

## Legacy BusyBox edition

The original build remains available separately: **Linux 6.13.3 + BusyBox + GRUB**,
a complete FHS tree and bootable BIOS/UEFI ISO. The instructions below apply to
that legacy edition; use [live/README.md](live/README.md) for the dual-boot desktop.

![mnOS boot screen](assets/screenshot.png)

## Features

- Complete FHS layout (`/bin /etc /usr /var /proc /sys ...`)
- busybox userland with 400+ applets (sh, vi, top, grep, awk, ...)
- Auto-login shell: once the boot logs settle it **clears the screen and prints a system-info card**
- Colored prompt `[\W]#` plus shortcuts `off` (poweroff), `rb` (reboot), `ll` (ls -l)
- GRUB menu and kernel framebuffer both at **1280×720 (16:9)**
- Dual console output (serial + VGA): commands like `ls` and `mneofetch` render on both
- Single packed rootfs in initramfs, fast boot (~3 s to shell)
- Local **`ai`** chatbot: pure-Python MiniMind2 (llama) inference on CPU — just type `ai`

## Run

After building, the launcher finds the ISO next to the repository:

```bash
./run.sh                          # QEMU window, 1024 MiB RAM
./run.sh --serial                  # terminal mode; Ctrl-A then X to quit QEMU
./run.sh --memory 2048 /path/to/mnOS.iso
```

It uses KVM when `/dev/kvm` is accessible, otherwise QEMU's software emulation.
Set `QEMU=/path/to/qemu-system-x86_64` to use a different QEMU executable.

To launch QEMU directly:

```bash
# graphical (GRUB menu + VGA console)
qemu-system-x86_64 -enable-kvm -m 512 -cdrom mnOSv1.7.iso

# serial console (interact right in your terminal)
qemu-system-x86_64 -enable-kvm -m 512 -cdrom mnOSv1.7.iso -nographic
```

> The bundled `ai` model needs more headroom — start QEMU with `-m 1024` before running `ai`.

### Optional desktop

![mnOS framebuffer desktop running in QEMU](assets/desktop.png)

```bash
WITH_DESKTOP=1 DEFAULT_DESKTOP=1 ./build.sh
./run.sh
```

This adds a small framebuffer launcher with a terminal, live memory/uptime view,
keyboard help, and shutdown/restart confirmation. Press **T** for the terminal;
type `exit` to return. Tab/arrow keys and Enter select a card, **S** opens system
information, and Esc goes back or exits to the console. Mouse input is supported
when the kernel provides `/dev/input/mice`. The bundled kernel can use the
keyboard; the optional QEMU kernel below also enables mouse input.

The desktop needs a Linux virtual console and a true-color framebuffer of at
least 640×480. `start-desktop` opens it on tty2 from a running shell. The serial
console stays available, and the original console entry remains in GRUB. Leave
out `DEFAULT_DESKTOP=1` to keep that entry as the default. Files still live in RAM.

Building the desktop requires a C compiler and static libc development files
(for example, `gcc libc6-dev` on Debian). No display server or extra GUI runtime
is needed. Its public-domain bitmap font is vendored with attribution.

### Optional QEMU kernel

The repository's `boot/bzImage` is kept as the default. To build an alternative,
unpack the **Linux 6.13.3** release from kernel.org, then run:

```bash
JOBS=4 ./tools/build-qemu-kernel.sh /path/to/linux-6.13.3 /path/to/kernel-build
KERNEL_IMAGE=/path/to/kernel-build/arch/x86/boot/bzImage \
  WITH_DESKTOP=1 DEFAULT_DESKTOP=1 ./build.sh
```

This profile starts from `x86_64_defconfig`, targets up to 8 virtual CPUs, uses
size optimization, and leaves out audio, wireless, media and physical GPU drivers.
It keeps the Bochs/virtio display and input drivers, adds the mouse device, and
embeds its configuration at `/proc/config.gz`. It is intended for QEMU; use the
default kernel for other hardware. The resolved configuration is also saved in
the output directory as `.config`. Building it needs `make gcc bc flex bison
libelf-dev libssl-dev` and the usual libc development files.

Development checks:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
GEN_INIT_CPIO=/path/to/gen_init_cpio ./tests/test-build-mountpoints.sh
cc -fsanitize=address,undefined -g tests/test_desktop.c -o /tmp/test-desktop
/tmp/test-desktop
```

## Build

### One-shot build

```bash
./build.sh          # repack initramfs and produce ../mnOSv1.7.iso
```

Requires `grub-mkrescue`, `cpio`, `gzip`, and the kernel's `gen_init_cpio`
(override with `GEN_INIT_CPIO=/path/to/gen_init_cpio`; `OUT=...` sets the output
path; `KERNEL_SRC=...` sets the kernel source dir).

### Prerequisites (kernel / busybox)

#### 1. Kernel

```bash
git clone --depth=1 -b v6.13.3 https://github.com/torvalds/linux.git
cd linux
make olddefconfig          # key options: BLK_DEV_INITRD, DEVTMPFS, VT, DRM_BOCHS,
                           # FRAMEBUFFER_CONSOLE, SERIAL_8250_CONSOLE, ISO9660_FS
make -j$(nproc)
cp arch/x86/boot/bzImage ../
```

#### 2. busybox

```bash
git clone https://git.busybox.net/busybox
cd busybox
make defconfig             # dynamic linking (CONFIG_STATIC is not set)
make -j$(nproc)
make CONFIG_PREFIX=../mnOS install
```

The rootfs also needs the glibc libraries busybox depends on:

```bash
mkdir -p mnOS/lib/x86_64-linux-gnu
cp -a /lib/x86_64-linux-gnu/{libc,libm,libresolv,libnss_files,libnss_dns}-2.31.so* \
      /lib/x86_64-linux-gnu/ld-2.31.so mnOS/lib/x86_64-linux-gnu/
ln -sf /lib/x86_64-linux-gnu/ld-2.31.so mnOS/lib64/ld-linux-x86-64.so.2
printf '/lib/x86_64-linux-gnu\n/usr/lib/x86_64-linux-gnu\n' > mnOS/etc/ld.so.conf
ldconfig -r mnOS
```

#### 3. initramfs (no root needed)

Device nodes are written straight into the cpio header with the kernel's
`gen_init_cpio` — no `mknod`, no root:

```bash
cat > devspec <<'EOF'
dir /proc 0755 0 0
dir /sys 0755 0 0
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/tty      0666 0 0 c 5 0
nod /dev/tty0     0600 0 0 c 4 0
nod /dev/tty1     0600 0 0 c 4 1
nod /dev/ttyS0    0666 0 0 c 4 64
nod /dev/null     0666 0 0 c 1 3
nod /dev/zero     0666 0 0 c 1 5
nod /dev/random   0666 0 0 c 1 8
nod /dev/urandom  0666 0 0 c 1 9
nod /dev/ptmx     0666 0 0 c 5 2
EOF
linux/usr/gen_init_cpio devspec > dev.cpio

# concatenate two archives: devices first, filesystem second (the kernel
# understands concatenated cpio)
cd mnOS
find . -path ./boot -prune -o -path ./.git -prune -o -print | cpio --owner 0:0 -H newc -o > ../tree.cpio
cd ..
cat dev.cpio tree.cpio | gzip -9 > mnOS/boot/initramfs.cpio.gz
```

#### 4. Build the ISO

```bash
mkdir -p staging/boot/grub
cp mnOS/boot/bzImage        staging/boot/
cp mnOS/boot/initramfs.cpio.gz staging/boot/

cat > staging/boot/grub/grub.cfg <<'EOF'
insmod all_video
if loadfont unicode; then
    set gfxmode=1280x720
    set gfxpayload=1280x720
    terminal_output gfxterm
fi
set timeout=5
set default=0
menuentry "mnOS" {
    linux /boot/bzImage video=Virtual-1:1280x720 console=tty0 console=ttyS0,115200
    initrd /boot/initramfs.cpio.gz
}
EOF

grub-mkrescue -o mnOSv1.7.iso staging/
```

## Layout

```text
mnOS/
├── init            # PID 1: mount proc/sys/devtmpfs/pts, then exec /sbin/init
├── bin/            # busybox + applet symlinks (includes the mneofetch card)
├── sbin/           # init, getty, ...
├── etc/
│   ├── inittab     # sysinit + dual-console shell
│   ├── init.d/rcS  # mount fs → wait for logs to settle → clear → print the card
│   ├── profile     # colored PS1, shortcut aliases
│   └── ...
├── boot/           # bzImage + initramfs.cpio.gz
├── opt/ai/         # MiniMind2 inference engine + model + numpy/regex
├── assets/         # README screenshot
├── build.sh        # one-shot build script
├── lib/, lib64/    # glibc runtime and dynamic loader
└── proc/ sys/ dev/ ...  # standard FHS dirs (mounted at runtime)
```

## Technical notes

| Problem | Solution |
|---------|----------|
| Can't `mknod` without root | use the kernel's `gen_init_cpio` to emit device nodes into the cpio |
| Kernel stops auto-mounting devtmpfs once `/init` exists | mount proc/sysfs/devtmpfs manually in `/init` |
| Which console `/dev/console` points to | determined by the order of `console=` args; put `ttyS0` last |
| DRM ignores GRUB's `gfxpayload` resolution | force it with `video=Virtual-1:1280x720` |
| GRUB theme silently breaks | drop the `terminal-*` fields (they make theme parsing fail) |
| Concatenated cpio swallowed as a regular file | generate the archives separately and `cat` them; don't pipe into `find` |

## License

mnOS's own code (the `init` process, `build.sh`, the startup scripts under
`etc/` and `bin/mneofetch`) is licensed under **GPL-3.0-or-later** — see
[LICENSE](LICENSE).

The mnOS image is distributed as an **aggregate**: it bundles third-party
software under their own licenses, notably Linux and BusyBox (GPL-2.0-only),
glibc (LGPL-2.1-or-later), GRUB, nano and GNU make (GPL-3.0-or-later), Vim
(Vim License), Python (PSF License), tcc (LGPL-2.1), Git (GPL-2.0), NumPy
(BSD-3-Clause), regex and the MiniMind2 model (Apache-2.0). See
[NOTICE](NOTICE) for the full list, upstream sources and how to obtain the
Corresponding Source. The image is not relicensed as a whole.
