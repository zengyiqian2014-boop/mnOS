#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A boot-session-only overlay for already-built ISOs. The installed OS payload
# remains byte-identical; its installer service is disabled after installation.
set -euo pipefail
iso=$(realpath "${1:?ISO}")
arch=${2:?Architecture}
source_root=$(cd "$(dirname "$0")" && pwd)
case "$arch" in amd64|arm64) ;; *) exit 2 ;; esac
out=$(dirname "$iso")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$out" && sha256sum -c "$(basename "$iso").sha256")
cp "$iso.sha256" "$out/base-image-$arch.sha256.txt"
install -d -m 755 "$work/root/usr/lib/mnos"
install -m 755 "$source_root/overlay/usr/lib/mnos/installer-session" "$work/root/usr/lib/mnos/installer-session"
mksquashfs "$work/root" "$work/zz-installer-display.squashfs" -noappend -all-root -comp xz -processors 2 >/dev/null
xorriso -osirrox on -indev "$iso" -extract /live/filesystem.sha256 "$work/filesystem.sha256" >/dev/null 2>&1
(cd "$work" && sha256sum zz-installer-display.squashfs >> filesystem.sha256)
xorriso -indev "$iso" -outdev "$work/patched.iso" -boot_image any replay \
    -map "$work/zz-installer-display.squashfs" /live/zz-installer-display.squashfs \
    -map "$work/filesystem.sha256" /live/filesystem.sha256 >/dev/null 2>&1
mv "$work/patched.iso" "$iso"
(cd "$out" && sha256sum "$(basename "$iso")" > "$(basename "$iso").sha256")
git -C "$source_root" rev-parse HEAD > "$out/boot-overlay-$arch.txt"
