#!/bin/sh
# mnOS build script
# Copyright (C) 2026 gtl-m
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   rootfs (this directory)  ->  boot/initramfs.cpio.gz  ->  mnOSv<ver>.iso
#
# usage:  ./build.sh
#
# deps:   grub-mkrescue, cpio, gzip, and the kernel's gen_init_cpio.
#         override the latter with GEN_INIT_CPIO=/path/to/gen_init_cpio
#
set -e

ROOT=$(cd "$(dirname "$0")" && pwd)
VERSION=1.7
OUT=${OUT:-$(dirname "$ROOT")/mnOSv${VERSION}.iso}
KERNEL_SRC=${KERNEL_SRC:-$HOME/kernel/linux-6.13.3}
GEN_INIT_CPIO=${GEN_INIT_CPIO:-$KERNEL_SRC/usr/gen_init_cpio}
KERNEL_IMAGE=${KERNEL_IMAGE:-$ROOT/boot/bzImage}
WITH_DESKTOP=${WITH_DESKTOP:-0}
DEFAULT_DESKTOP=${DEFAULT_DESKTOP:-0}
case "$WITH_DESKTOP:$DEFAULT_DESKTOP" in
    [01]:[01]) ;;
    *) echo 'error: WITH_DESKTOP and DEFAULT_DESKTOP must be 0 or 1' >&2; exit 1 ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for f in "$KERNEL_IMAGE"; do
	[ -f "$f" ] || { echo "error: missing $f" >&2; exit 1; }
done
[ -x "$GEN_INIT_CPIO" ] || {
	echo "error: gen_init_cpio not found: $GEN_INIT_CPIO" >&2
	echo "       set GEN_INIT_CPIO=/path/to/gen_init_cpio" >&2
	exit 1
}

echo "==> device nodes (gen_init_cpio, no root needed)"
cat > "$WORK/devspec" <<'SPEC'
dir /proc 0755 0 0
dir /sys 0755 0 0
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/tty     0666 0 0 c 5 0
nod /dev/tty0    0600 0 0 c 4 0
nod /dev/tty1    0600 0 0 c 4 1
nod /dev/tty2    0600 0 0 c 4 2
nod /dev/tty3    0600 0 0 c 4 3
nod /dev/ttyS0   0666 0 0 c 4 64
nod /dev/null    0666 0 0 c 1 3
nod /dev/zero    0666 0 0 c 1 5
nod /dev/random  0666 0 0 c 1 8
nod /dev/urandom 0666 0 0 c 1 9
nod /dev/ptmx    0666 0 0 c 5 2
nod /dev/kmsg    0644 0 0 c 1 11
SPEC
"$GEN_INIT_CPIO" "$WORK/devspec" > "$WORK/dev.cpio"

echo "==> root filesystem archive"
( cd "$ROOT"
  find . \
	-path ./boot -prune -o \
	-path ./.git -prune -o \
	-path ./assets -prune -o \
	-path ./releases -prune -o \
	-path ./tests -prune -o \
	-path ./desktop -prune -o \
	-path ./tools -prune -o \
	-name build.sh -prune -o \
	-name run.sh -prune -o \
	-name 'README*' -prune -o \
	-print | cpio --owner 0:0 -H newc -o --quiet ) > "$WORK/tree.cpio"

: > "$WORK/desktop.cpio"
if [ "$WITH_DESKTOP" = 1 ]; then
    echo "==> framebuffer desktop"
    sh "$ROOT/desktop/build.sh" "$WORK/overlay/usr/bin/mnos-desktop"
    ( cd "$WORK/overlay"; find . -print | cpio --owner 0:0 -H newc -o --quiet ) > "$WORK/desktop.cpio"
elif [ "$DEFAULT_DESKTOP" = 1 ]; then
    echo 'error: DEFAULT_DESKTOP=1 requires WITH_DESKTOP=1' >&2
    exit 1
fi

echo "==> initramfs (device cpio + rootfs, concatenated)"
cat "$WORK/dev.cpio" "$WORK/tree.cpio" "$WORK/desktop.cpio" | gzip -9 > "$ROOT/boot/initramfs.cpio.gz"

echo "==> squash staging tree"
mkdir -p "$WORK/iso/boot/grub"
cp "$KERNEL_IMAGE" "$WORK/iso/boot/bzImage"
cp "$ROOT/boot/initramfs.cpio.gz" "$WORK/iso/boot/"
cat > "$WORK/iso/boot/grub/grub.cfg" <<EOF
insmod all_video
if loadfont unicode; then
    set gfxmode=1280x720
    set gfxpayload=1280x720
    terminal_output gfxterm
fi

set timeout=5
set default=$DEFAULT_DESKTOP

menuentry "mnOS v${VERSION}" {
    linux /boot/bzImage video=Virtual-1:1280x720 console=tty0 console=ttyS0,115200
    initrd /boot/initramfs.cpio.gz
}
EOF
if [ "$WITH_DESKTOP" = 1 ]; then
    cat >> "$WORK/iso/boot/grub/grub.cfg" <<EOF

menuentry "mnOS v${VERSION} desktop" {
    linux /boot/bzImage video=Virtual-1:1280x720 console=tty0 console=ttyS0,115200 quiet loglevel=3 mnos.desktop=1
    initrd /boot/initramfs.cpio.gz
}
EOF
fi

echo "==> building ISO"
grub-mkrescue -o "$OUT" "$WORK/iso"

echo "done: $OUT"
