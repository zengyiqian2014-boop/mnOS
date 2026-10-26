# Two mnOS editions

| | Native Edition | Linux Edition |
| --- | --- | --- |
| Host kernel | Original freestanding C/assembly mnOS | Debian's maintained Linux kernel |
| Desktop | Original framebuffer windows, launcher, files, editor, terminal and settings | Xfce with mnOS C/C++ tool and management centers |
| Installation | Graphical UEFI installer, MNFS; AMD64 and ARM64 media | Offline installer, ext4; AMD64 and ARM64 media |
| Linux applications | Bundled Linux 6.12 guest inside the original C RV64IMAC/Sv39 interpreter | Native Linux binaries for the machine architecture |
| Commands | Native kernel/account/file/process commands plus the guest's BusyBox utilities | GNU/Linux commands, package manager, development and CTF tools |
| Management | `system`, `kernel`, `linux`, original Settings/Monitor windows | `mnos-admin`, `kernel`, management center and existing system managers |

The Native Edition **boots the original mnOS kernel**. Its Linux window executes
a separate Linux guest kernel and actual Linux user programs; it neither boots
the host into Linux nor forwards commands to a hidden Linux host. Its interpreter,
virtual memory translation, instruction decoder, SBI interface and desktop
integration are original C code. Linux, BusyBox, musl and compiler runtime inside
the guest are upstream components with their own licenses.

The guest has 64 MiB RAM and one emulated RV64IMAC CPU with Sv39 and S/U modes.
Its Linux ABI is RV64IMAC/LP64; arbitrary AMD64/ARM64 Linux binaries, floating-point
ISA, vectors and graphical Linux applications are **not supported**. BusyBox's
compiled command inventory includes commands whose devices/services are absent;
the inventory is not a claim that every operation works. Files, text pipelines,
process queries, checksums and shell applications are the initial tested scope.
There is no network adapter, GPU, host filesystem, disk or device passthrough.
Guest files are temporary and disappear on restart/stop or host account changes.
The emulator runs as a bounded, cooperatively serviced native kernel component;
it is not yet isolated in a separate native user process. This is an experimental
execution boundary, not a security certification.

Firefox, official VS Code, Wireshark, GHex, SQLite Browser, Nmap, GDB, Pwntools,
SQLmap and other full desktop/CTF tools are in **Linux Edition**. They do not run
inside the initial native Linux guest. Python is present only where third-party
tools need it. Our new implementation is C/C++/assembly with shell build scripts.

Both editions target their documented VM and firmware/device configurations,
not every physical computer. Native device support remains narrower than Linux.
ARM64 QEMU `virt`/UEFI is the test target; Apple Virtualization.framework and
arbitrary Apple Silicon boards are not claimed supported.

## Native operation

Open **Linux environment** from the launcher and press Start, or enter
`linux start` in the host Terminal. `linux pause`, `linux resume`, `linux status`,
`linux stop` and `linux restart` control the guest. Stop/restart discard guest
files; closing the window only hides it. F5 returns to the host Terminal.
`system status`, `system processes`, `system storage`, `system guest` report
the real host services. `help` is English; `help -cn` is Chinese. The guest has
its own `help`, `help -cn`, `busybox --list` and `COMMAND --help`.

## Linux operation

Open **mnOS System Management** for services, accounts, disks, network, logs,
updates, security, kernel settings and tools. `mnos-admin help` and
`mnos-admin help -cn` describe the CLI. `mnos-center --list-commands` exports the
actual installed command index. Queries use the current account; root-required
changes request or require normal authentication. No directory-name convention
or wrapper grants implicit root.

## Build and evidence

Native guest source archives have pinned SHA256 hashes in `native/guest/build.sh`.
Build with the native builder, run that script, then run
`GUEST=1 native/installer/build.sh`. `MNOS_TEST_LINUX_GUEST=1` enables actual
Linux-in-native tests in `native/tests/desktop_vm.cpp`. Host ASan/UBSan tests
cover emulator bounds, malformed instructions, Sv39 permissions, privilege
checks and interrupts; VM tests must additionally execute real guest commands.
The guest licenses/configs/source checksums accompany its release.

Linux builds default to `EDITION=linux ./live/build.sh installer amd64` (or
`arm64` on an ARM64 runner). The old combined boot layout is only available with
explicit `EDITION=combined`; it is not the native guest runtime.
