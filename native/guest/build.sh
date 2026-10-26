#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
WORK=${GUEST_BUILD_DIR:-$ROOT/build/guest-source}
OUT=$ROOT/build/guest
JOBS=${JOBS:-4}
mkdir -p "$WORK" "$OUT"
fetch() {
    local name=$1 sha=$2 url=$3
    if ! printf '%s  %s\n' "$sha" "$WORK/$name" | sha256sum -c --status; then
        curl --fail --location --retry 3 "$url" -o "$WORK/$name.part"
        printf '%s  %s\n' "$sha" "$WORK/$name.part" | sha256sum -c --strict
        mv "$WORK/$name.part" "$WORK/$name"
    fi
    printf '%s  %s\n' "$sha" "$name" >> "$OUT/SOURCES.sha256"
}
: > "$OUT/SOURCES.sha256"
fetch linux-6.12.114.tar.xz 7a8589912bd7a8adb07fed074283652cf482a35facef8954554654f0154b4af3 https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.114.tar.xz
fetch musl-1.2.5.tar.gz a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 https://sources.buildroot.net/musl/musl-1.2.5.tar.gz
fetch busybox-1.37.0.tar.bz2 3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4 https://sources.buildroot.net/busybox/busybox-1.37.0.tar.bz2
for archive in linux-6.12.114.tar.xz musl-1.2.5.tar.gz busybox-1.37.0.tar.bz2; do
    dir=${archive%%.tar*}
    [ -d "$WORK/$dir" ] || tar -xf "$WORK/$archive" -C "$WORK"
done
PREFIX=$WORK/toolchain
if [ ! -f "$PREFIX/lib/libc.a" ]; then
    (cd "$WORK/musl-1.2.5"
     CC='riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64' AR=riscv64-linux-gnu-ar RANLIB=riscv64-linux-gnu-ranlib ./configure --target=riscv64-linux-musl --disable-shared --prefix="$PREFIX"
     make -j"$JOBS"
     make install)
fi
sh "$WORK/musl-1.2.5/tools/musl-gcc.specs.sh" "$PREFIX/include" "$PREFIX/lib" /lib/ld-musl-riscv64.so.1 > "$PREFIX/lib/musl-gcc.specs"
# Use the compiler's real soft-float LP64 multilib. Debian's Linux cross-GCC
# default LP64D library has a different ABI and cannot be substituted.
cp "$(riscv64-unknown-elf-gcc -march=rv64imac -mabi=lp64 -print-libgcc-file-name)" "$PREFIX/lib/libgcc.a"
sed -i -e 's/libgcc.a%s %:if-exists(libgcc_eh.a%s)/-lgcc/' \
  -e 's/Scrt1.o/crt1.o/; s/ crtbeginS.o%s//; s/crtendS.o%s //' "$PREFIX/lib/musl-gcc.specs"
make -C "$WORK/linux-6.12.114" O="$WORK/headers-build" ARCH=riscv INSTALL_HDR_PATH="$PREFIX" headers_install
cat > "$WORK/guest-cc" <<EOF
#!/bin/sh
exec riscv64-linux-gnu-gcc -march=rv64imac -mabi=lp64 -specs='$PREFIX/lib/musl-gcc.specs' "\$@"
EOF
chmod +x "$WORK/guest-cc"
SYS=$WORK/rootfs
mkdir -p "$SYS"/{dev,proc,sys,tmp,run,root,etc,usr/bin}
(cd "$WORK/busybox-1.37.0"
 make ARCH=riscv defconfig
 sed -i -e 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
   -e 's/CONFIG_TC=y/# CONFIG_TC is not set/' \
   -e 's/CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' \
   -e 's/CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' \
   -e 's/CONFIG_FEATURE_SH_STANDALONE=y/# CONFIG_FEATURE_SH_STANDALONE is not set/' .config
 make ARCH=riscv -j"$JOBS" CC="$WORK/guest-cc" HOSTCC=gcc CROSS_COMPILE=riscv64-linux-gnu- SKIP_STRIP=y
 make ARCH=riscv CC="$WORK/guest-cc" CROSS_COMPILE=riscv64-linux-gnu- CONFIG_PREFIX="$SYS" install
 cp .config "$OUT/busybox.config")
install -m755 "$ROOT/guest/init" "$SYS/init"
install -m755 "$ROOT/guest/help" "$SYS/usr/bin/help"
printf 'root:x:0:0:Linux guest:/root:/bin/sh\n' > "$SYS/etc/passwd"
printf 'root:x:0:\n' > "$SYS/etc/group"
printf 'mnOS Linux guest\n' > "$SYS/etc/issue"
(cd "$SYS"; find . -print0 | LC_ALL=C sort -z | cpio --null --reproducible -o -H newc > "$OUT/rootfs.cpio")
K=$WORK/linux-6.12.114
# Generate device records without mknod privileges on the build host.
gcc -std=gnu11 -O2 "$K/usr/gen_init_cpio.c" -o "$WORK/gen-init-cpio"
printf 'nod /dev/console 0600 0 0 c 5 1\nnod /dev/null 0666 0 0 c 1 3\n' > "$WORK/devices.list"
"$WORK/gen-init-cpio" "$WORK/devices.list" >> "$OUT/rootfs.cpio"
KB=$WORK/kernel-build
make -C "$K" O="$KB" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- tinyconfig
cfg=(--file "$KB/.config")
for name in 64BIT MMU NONPORTABLE RISCV_SBI RISCV_SBI_V01 SOC_VIRT RISCV_ISA_C \
    PRINTK TTY HVC_RISCV_SBI SERIAL_EARLYCON_RISCV_SBI BLK_DEV_INITRD \
    BINFMT_ELF BINFMT_SCRIPT PROC_FS SYSFS TMPFS DEVTMPFS UNIX NET INET \
    FUTEX EPOLL EVENTFD SIGNALFD TIMERFD POSIX_TIMERS MULTIUSER \
    SYSVIPC SYSCTL FILE_LOCKING COREDUMP STRICT_KERNEL_RWX STRICT_MODULE_RWX; do cfg+=(-e "$name"); done
for name in SMP FPU VECTOR RISCV_ISA_V RANDOMIZE_BASE STACKPROTECTOR STRIP_ASM_SYMS; do cfg+=(-d "$name"); done
"$K/scripts/config" "${cfg[@]}" --set-str INITRAMFS_SOURCE "$OUT/rootfs.cpio" --set-val INITRAMFS_ROOT_UID 0 --set-val INITRAMFS_ROOT_GID 0
make -C "$K" O="$KB" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$K" O="$KB" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j"$JOBS" Image
cp "$KB/arch/riscv/boot/Image" "$OUT/Image"
cp "$KB/.config" "$OUT/linux.config"
dtc -I dts -O dtb -o "$OUT/machine.dtb" "$ROOT/guest/machine.dts"
cp "$WORK/busybox-1.37.0/busybox" "$OUT/busybox"
(cd "$OUT"; sha256sum Image machine.dtb busybox *.config > SHA256SUMS)
printf 'Guest built: %s\n' "$OUT/Image"
