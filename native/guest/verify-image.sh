#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Check the files inside the packaged guest, not just the staging directory.
set -euo pipefail
export LC_ALL=C
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/guest
LIVE=${MNOS_LIVE_SOURCE:-$ROOT/../live/src}
image=${1:-$OUT/rootfs.squashfs}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
paths=(usr/local/bin/mnos-apps usr/local/bin/mnos-gui usr/local/bin/mnos-pkg)
unsquashfs -no-progress -d "$work/root" "$image" "${paths[@]}" > "$work/extract.log" 2>&1 || {
    cat "$work/extract.log" >&2
    exit 1
}
matches() {
    # -cat resolves merged-/usr links (for example /sbin -> usr/sbin).
    # Selective tree extraction alone would leave that target unextracted.
    if ! unsquashfs -cat "$image" "$2" > "$work/content" ||
       ! cmp -s -- "$1" "$work/content"; then
        printf 'Guest image mismatch: /%s does not match %s\n' "$2" "$1" >&2
        exit 1
    fi
}
matches "$ROOT/guest/debian-init" sbin/mnos-init
matches "$ROOT/guest/debian-help" usr/local/bin/help
matches "$ROOT/guest/dhcp" usr/local/lib/mnos-dhcp
matches "$ROOT/guest/xorg.conf" etc/X11/xorg.conf
matches "$ROOT/guest/desktop-session" usr/local/lib/mnos/desktop-session
for name in app-center.cpp gui-client.cpp; do
    matches "$ROOT/guest/$name" "usr/src/mnos-guest/$name"
done
for name in package.cpp install-notice.hpp mnos-locale.h; do
    matches "$LIVE/$name" "usr/src/mnos-guest/$name"
done
for app in mnos-apps mnos-gui mnos-pkg; do
    test -x "$work/root/usr/local/bin/$app"
    readelf -h "$work/root/usr/local/bin/$app" > "$work/header"
    grep -Eq 'Machine: +RISC-V' "$work/header"
done
for name in SHMEM TMPFS MEMFD_CREATE USER_NS SECCOMP_FILTER; do
    grep -qx "CONFIG_${name}=y" "$OUT/linux.config"
done
(cd "$OUT" && sha256sum -c SHA256SUMS)
printf 'GUEST IMAGE INPUTS PASS: packaged init, GUI session, application sources and kernel configuration\n'
