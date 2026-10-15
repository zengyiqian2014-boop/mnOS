# Implementation and dependency boundaries

mnOS has two separate boot environments. Selecting the Linux environment does
not execute the original kernel under Linux or translate its APIs.

| Component | Implementation and origin |
| --- | --- |
| Native kernel, memory manager, scheduler, interrupt/SMP code, local accounts and graphical console | mnOS C and x86 assembly under `native/`; build links no Linux kernel code or Python interpreter |
| Native MNFS and Linux image utility | mnOS experimental filesystem, documented in `native/FILESYSTEM.md`; no APFS compatibility claim |
| Linux system center, search menu, first account setup, wallpaper initialization and network policy | mnOS C++17 under `live/src/`; installed as compiled ELF programs, no Python entry point |
| Linux `kernel` command and network capability gate | mnOS C under `live/src/`; ordinary queries and privileged changes are separated |
| Bootloader | GRUB; Debian shim/GRUB signature chain for the Linux entry |
| Linux compatibility environment | Debian 13, Linux, systemd, Xorg, Xfce, Mesa and vendor firmware; these are third-party components |
| GUI libraries, themes and application icons | GTK/GIO/JSON-GLib, Arc and Papirus; their existing licenses and names are retained |
| Installer, storage, authentication and administration | Calamares, Linux filesystem drivers, PAM/shadow tools, sudo/polkit and Cockpit; not claimed as original mnOS implementations |
| Editors, browser, development and security tools | Third-party applications, including official Microsoft VS Code and Firefox ESR; each image records exact packages and versions |

The 34 px panel, icon-only task buttons, compact two-column menu and search field
were studied in the [Kali Xfce defaults](https://gitlab.com/kalilinux/packages/kali-themes).
The mnOS menu is a new C++ implementation; it does not include Whisker source or
Kali logos, branding or wallpaper. The panel in the Linux environment is still
Xfce. The mnOS mark is an original SVG and the coast wallpaper is generated art;
see the background asset's README.

## Python boundary

No mnOS runtime component in the current dual-boot overlay is implemented in
Python. The native entry contains no Python runtime. The Linux image does not
request Python development headers, pytest, global pip or pipx as default tools.
Python and libraries required by Pwntools, SQLmap, mitmproxy, TuneD, the installer
or other selected third-party software remain available. Some of those tools
have substantial dependency trees: this is dependency-based inclusion, not a
claim that the final image contains only a tiny Python standard library.
Mitmproxy uses a dedicated environment whose pinned wheels and hashes are
recorded in the image. The repository's Python QA/build harnesses run outside
normal OS operation and are not installed as system services or GUI programs.
The separately documented legacy BusyBox edition is not copied into these ISOs.

## Remaining original-kernel work

The original entry does not yet provide the Linux/POSIX application ABI, general
USB/Wi-Fi/NVMe drivers, a production compositor, or GPU 3D acceleration. Firefox
and VS Code currently run in the separate Linux environment. Replacing those
platform components requires additional original implementations and hardware
validation; renaming third-party packages would not meet that goal.
