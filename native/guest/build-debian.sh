#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run in a disposable root builder with a registered qemu-riscv64 binfmt handler.
# No QEMU, bootstrap tools, Python or Java from the builder enter the filesystem.
set -euo pipefail
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
SYS=${GUEST_DEBIAN_ROOT:-$ROOT/build/debian-rootfs}
MIRROR=${DEBIAN_MIRROR:-http://deb.debian.org/debian}
PACKAGES=apt,ca-certificates,iproute2,iputils-ping,procps,nano,curl,xserver-xorg-core,xserver-xorg-video-fbdev,xserver-xorg-input-evdev,xinit,xterm,twm,xfonts-base,x11-apps,x11-xserver-utils,fonts-dejavu-core,dbus,dbus-x11,e2fsprogs,sudo
test "$(id -u)" = 0 || { echo 'Run this build in the native builder as root.' >&2; exit 1; }
test -s "$ROOT/build/guest/busybox" || { echo 'Run guest/build.sh first.' >&2; exit 1; }
if [ ! -f "$SYS/.mnos-bootstrap-complete" ]; then
    if [ -e "$SYS/etc/debian_version" ]; then
        echo 'Incomplete rootfs exists. Use a new GUEST_DEBIAN_ROOT directory.' >&2; exit 1
    fi
    debootstrap --arch=riscv64 --variant=minbase --include="$PACKAGES" trixie "$SYS" "$MIRROR"
    touch "$SYS/.mnos-bootstrap-complete"
fi
cat > "$SYS/etc/apt/sources.list" <<EOF
deb $MIRROR trixie main
deb http://deb.debian.org/debian-security trixie-security main
deb $MIRROR trixie-updates main
EOF
printf '#!/bin/sh\nexit 101\n' > "$SYS/usr/sbin/policy-rc.d"
chmod 755 "$SYS/usr/sbin/policy-rc.d"
mount -t proc proc "$SYS/proc"
trap 'umount "$SYS/proc"' EXIT
IFS=, read -r -a packages <<< "$PACKAGES"
chroot "$SYS" apt-get update | tee "$ROOT/build/guest/debian-build.log"
chroot "$SYS" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}" | tee -a "$ROOT/build/guest/debian-build.log"
chroot "$SYS" dpkg-query -W '-f=${db:Status-Abbrev}\t${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n' |
    awk -F '\t' '$1 == "ii " {print $2 "\t" $3 "\t" $4 "\t" $5}' > "$ROOT/build/guest/DEBIAN-PACKAGES.tsv"
chroot "$SYS" dpkg --audit > "$ROOT/build/guest/debian-audit.txt"
test ! -s "$ROOT/build/guest/debian-audit.txt"
umount "$SYS/proc"
trap - EXIT
# Keep complete upstream copyright/licensing files in /usr/share/doc.
bash "$ROOT/guest/prepare-debian.sh" "$SYS"
