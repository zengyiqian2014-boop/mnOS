#!/bin/sh
# Optional x86-64 QEMU kernel. The general-purpose bundled kernel is untouched.
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
SRC=${1:?usage: tools/build-qemu-kernel.sh LINUX_SOURCE [OUTPUT_DIR]}
SRC=$(CDPATH= cd "$SRC" && pwd)
OUT=${2:-$(dirname "$SRC")/mnos-kernel-build}
mkdir -p "$OUT"
OUT=$(CDPATH= cd "$OUT" && pwd)
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
make -C "$SRC" O="$OUT" x86_64_defconfig
"$SRC/scripts/config" --file "$OUT/.config" \
    --set-str LOCALVERSION '-mnos-qemu' --disable LOCALVERSION_AUTO \
    --set-val NR_CPUS 8 --enable HZ_250 --disable HZ_1000 \
    --enable CC_OPTIMIZE_FOR_SIZE --disable CC_OPTIMIZE_FOR_PERFORMANCE \
    --enable IKCONFIG --enable IKCONFIG_PROC \
    --enable BLK_DEV_INITRD --enable RD_GZIP \
    --enable DEVTMPFS --enable DEVTMPFS_MOUNT --enable TMPFS \
    --enable DRM --enable DRM_BOCHS --enable DRM_VIRTIO_GPU \
    --enable DRM_FBDEV_EMULATION --enable FB --enable FRAMEBUFFER_CONSOLE \
    --enable INPUT_EVDEV --enable INPUT_MOUSEDEV --enable INPUT_MOUSEDEV_PSAUX \
    --enable VIRTIO_PCI --enable VIRTIO_BLK --enable VIRTIO_NET \
    --disable SOUND --disable MEDIA_SUPPORT --disable WLAN --disable WIRELESS \
    --disable DRM_I915 --disable DRM_RADEON --disable DRM_AMDGPU --disable DRM_NOUVEAU \
    --disable DEBUG_KERNEL --disable DEBUG_INFO --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
    --enable DEBUG_INFO_NONE
make -C "$SRC" O="$OUT" olddefconfig
make -C "$SRC" O="$OUT" -j"$JOBS" bzImage
printf '\nKernel: %s\nConfiguration: %s\n' "$OUT/arch/x86/boot/bzImage" "$OUT/.config"
