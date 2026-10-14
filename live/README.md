# mnOS dual-boot ISO

Each ISO offers two independent environments: the original freestanding
[mnOS native kernel](../native/README.md), and a usable desktop built from
maintained Debian 13 packages (Linux 6.12 LTS, Xfce, Firefox ESR, system management,
CTF utilities and Calamares). The native kernel has its own framebuffer UI,
preemptive scheduler and ring-3 isolation; it does not run Linux applications.
The original BusyBox build remains available from the repository root.

Two x86_64 profiles are built:

| Profile | Purpose | Extras |
| --- | --- | --- |
| `qemu` | Virtual machines | VirtIO, QEMU Guest Agent, SPICE agent; compositor off |
| `hardware` | General PCs/laptops | Common GPU/network/Wi-Fi/audio firmware, CPU microcode, Bluetooth |

See [DESIGN.md](DESIGN.md) for kernel choices, RHEL design references, tuning,
security boundaries and hardware limitations. There is no default account password.
First boot asks you to create a local administrator; root login remains locked.

## Build

Requires GCC, binutils, make, Docker with BuildKit, an x86_64 build host, network access, 4 CPU threads,
at least 8 GiB of host RAM and approximately 25 GiB of free space when using a
normal overlay Docker storage driver. VFS needs more space for full layer copies.
Package downloads use signed Debian repositories. Image tools run in containers;
the scripts do not install packages or start OS services on the host.

```sh
./live/build.sh qemu
./live/build.sh hardware
```

Output goes to `live/out/`. Each ISO has its own SHA256 file, package/source
version manifest and resolved kernel configuration. `OUT=/path` changes the
output directory. A normal build does not need additional CA certificates.
If a build network requires a private CA, place it in ignored `live/build-ca/`;
the build removes it from the final filesystem and rebuilds the trust store.

APT security updates and dependency resolution can change later builds. This is
a version-recorded build recipe, not a claim of bit-for-bit reproducibility.
The base container digest and mitmproxy version are pinned; manifests record the
actual package versions and Python wheel hashes used for each image.

## Boot and install

```sh
./live/run.sh                              # 4 GiB RAM, two vCPUs
UEFI=1 ./live/run.sh                       # OVMF required
MEMORY=8192 CPUS=4 ./live/run.sh /path/to/image.iso
qemu-img create -f qcow2 mnos.qcow2 32G
DISK="$PWD/mnos.qcow2" ./live/run.sh        # existing disk only
```

KVM is preferred when available; TCG is supported for functional testing.
Both ISOs have BIOS and UEFI boot paths and an appended EFI partition for USB
booting. The Linux signed EFI chain uses Debian shim/GRUB/kernel, not mnOS-owned keys.
The unsigned **native kernel requires Secure Boot to be off**. Linux is the
default entry; select the native entry to explore its UI and isolation tests.
Installed systems retain both entries, with Linux first.

To use an ISO on a physical PC, write it to a spare USB drive with an image
writer, selecting the correct target. The image writer erases that USB drive.
Boot the USB, create a Live account, then open **安装 mnOS / Install mnOS** in
the system center. The installer confirms partition changes before writing.

Live changes reside in RAM and disappear at shutdown. The installer creates a
separate persistent system and a new account from the original SquashFS. Back up
any existing data before changing partitions. No physical machine is implicitly
considered tested merely because its driver is present.

## Checks

```sh
python3 -m unittest discover -s live/tests -p 'test_*.py'
python3 live/tests/check_kernel.py live/out/kernel-qemu.config
docker run --rm --network none -v "$PWD/live/tests:/tests:ro" \
    mnos-live:qemu xvfb-run -a python3 /tests/gui_smoke.py
```

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
`/usr/share/doc/<package>/copyright`. `packages-<profile>.tsv` records source package
names and versions. Enable matching Debian `deb-src` repositories to retrieve
corresponding sources with `apt-get source source-package=source-version`;
historical versions can be located at <https://snapshot.debian.org/>.
The Linux kernel source is the Debian `linux` source package, including Debian
patches. mitmproxy wheels and resolved versions are listed under
`/usr/share/doc/mnos/`; its sources are available from PyPI and the upstream project.
Subscription documents and build credentials are never part of the distribution.
