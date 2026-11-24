#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-compile against the guest libraries. Build executables stay on the host.
set -euo pipefail
umask 022
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
SYS=$(cd -- "${1:?Usage: build-apps.sh DEBIAN_ROOT}" && pwd)
LIVE=${MNOS_LIVE_SOURCE:-$ROOT/../live/src}
test -f "$LIVE/install-notice.hpp"
stage=$SYS/usr/src/mnos-guest
install -d -m755 "$stage" "$SYS/usr/local/bin"
install -m644 "$ROOT/guest/"{app-center.cpp,gui-client.cpp,window-wait.cpp} "$stage/"
install -m644 "$LIVE/"{package.cpp,install-notice.hpp,mnos-locale.h} "$stage/"
export PKG_CONFIG_SYSROOT_DIR=$SYS
export PKG_CONFIG_LIBDIR=$SYS/usr/lib/riscv64-linux-gnu/pkgconfig:$SYS/usr/share/pkgconfig
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie -Wl,-z,relro,-z,now
       --sysroot="$SYS" -I"$stage" -L"$SYS/usr/lib/riscv64-linux-gnu" -Wl,-rpath-link,"$SYS/usr/lib/riscv64-linux-gnu")
read -r -a gtk_flags <<< "$(pkg-config --cflags --libs gtk+-3.0 gio-unix-2.0)"
riscv64-linux-gnu-g++ "${flags[@]}" "$stage/app-center.cpp" "${gtk_flags[@]}" -o "$SYS/usr/local/bin/mnos-apps"
riscv64-linux-gnu-g++ "${flags[@]}" "$stage/gui-client.cpp" -o "$SYS/usr/local/bin/mnos-gui"
read -r -a x11_flags <<< "$(pkg-config --cflags --libs x11)"
riscv64-linux-gnu-g++ "${flags[@]}" "$stage/window-wait.cpp" "${x11_flags[@]}" -o "$SYS/usr/local/bin/mnos-window-wait"
riscv64-linux-gnu-g++ "${flags[@]}" "$stage/package.cpp" -o "$SYS/usr/local/bin/mnos-pkg"
riscv64-linux-gnu-strip "$SYS/usr/local/bin/"{mnos-apps,mnos-gui,mnos-pkg,mnos-window-wait}
chmod 755 "$SYS/usr/local/bin/"{mnos-apps,mnos-gui,mnos-pkg,mnos-window-wait}
