# mnOS editions

[中文](EDITIONS.zh.md)

| | Native Edition | Linux Edition |
| --- | --- | --- |
| Host kernel | Original C/assembly mnOS | Debian Linux |
| Desktop | Original framebuffer GUI | Xfce with mnOS C/C++ management tools |
| Installer | UEFI, MNFS; AMD64 and ARM64 | Offline Linux installer, ext4; AMD64 and ARM64 |
| Linux apps | Optional Debian riscv64 guest in the original interpreter | Programs built for the physical CPU architecture |
| Packages | APT and riscv64/all DEBs inside the guest | APT and native AMD64/ARM64 DEBs |

The native host is not Linux. Its optional compatibility environment contains a
real Linux kernel and Debian applications, executed by the original RV64GC/Sv39
software CPU. It supports X11 framebuffer applications, keyboard/pointer input,
VirtIO Ethernet and a private persistent disk. This implementation is in the
current source; old USB-only preview ISOs do not acquire it automatically.
See the exact release manifest and [guest build/use guide](native/guest/README.md).

Linux simulation is off by default. `linux open applications` requests permission
and opens the searchable application center; `linux open desktop` shows Xterm.
`linux start` requests the console. F5 returns to the host terminal.
`linux apps` lists launchers, `linux status` shows the real state and RAM use.
Use `apt update`, `apt install PACKAGE` and `apt list --installed` inside Linux.
`mnos-gui list` discovers visible installed desktop entries. `mnos-gui open ID`
launches one; `mnos-gui run PROGRAM [ARG...]` handles programs without an entry.
The current source bundles Firefox ESR, Thunar and Mousepad; each release must
pass its native-window tests before those binaries are published.
Use a graphical Xterm for full-screen programs such as nano. The host serial
console is line-oriented. `help` is English and `help -cn` is Chinese.

The installed host saves each user's Linux packages/files in a private sparse
8 GiB data file. Run `poweroff` in the guest before disabling it; forced stop is
like a power cut. `linux stop` wipes RAM but retains that disk. `linux pause` and
`linux resume` retain the running state. Closing a window leaves jobs running;
logout disables simulation. No host directory or raw block device is shared.
The interpreter currently runs as a bounded native kernel component, not a
separate native user process; it is not a certified security boundary.

There is one interpreted CPU, no hardware virtualization, vector ISA or 3D
acceleration. RAM is configurable before startup with `linux memory MIB` or
`linux memory auto`, subject to available host memory. For APT/X11 use at least
768 MiB guest RAM and a 4 GiB host. Software interpretation has substantial
CPU overhead; this is not yet a high-performance production server system.

A DEB's architecture still matters: official AMD64/ARM64 VS Code packages cannot
run in a riscv64 guest. Linux Edition contains official VS Code, Firefox and the
broader CTF workbench for its CPU architecture. Those applications are not
claimed compatible with the native guest until individually tested. Third-party
Python dependencies remain only where their tools require them; mnOS code is
C/C++/assembly with shell build scripts.

Tested device configurations are documented per release. ARM64 QEMU virt/UEFI is
not equivalent to Apple Virtualization.framework or all ARM boards. Physical
Wi-Fi/NIC/GPU support remains narrower in the original native kernel than Linux.

Linux Edition's management center and `mnos-admin help` / `help -cn` cover
services, accounts, storage, network, logs and updates. Queries use ordinary
permissions; changes that require root explicitly authenticate. Native commands
use real host services through `system`, `kernel` and Settings. `support` lists
report.mnos@proton.me and wrmfwv@163.com; reports are never sent automatically.
