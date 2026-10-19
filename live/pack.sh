#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Private build-container entry point. Rootfs tar comes from stdin.
set -euo pipefail
test -f /.dockerenv || exit 1
case "${PROFILE:?}" in installer|qemu|hardware) ;; *) exit 2 ;; esac
case "${ARCH:-amd64}" in
    amd64) ARCH=amd64; EFI=x86_64-efi; EFI_SUFFIX=x64; BCJ=(-Xbcj x86); CONSOLE=ttyS0 ;;
    # The packaged squashfs tools do not offer an ARM64 BCJ encoder. ARM32
    # BCJ is NOT interchangeable: the ARM64 kernel disables that decoder.
    arm64) EFI=arm64-efi; EFI_SUFFIX=aa64; BCJ=(); CONSOLE=ttyAMA0 ;;
    *) exit 2 ;;
esac
mkdir -p /work/root /work/iso/{live,boot/grub/i386-pc}
tar --numeric-owner -xf - -C /work/root
ROOT=/work/root
ISO=/work/iso
cp -a --no-preserve=ownership /source/overlay/. "$ROOT/"
# Workspace directories may be private (0700). Never transfer that mode to /usr.
while IFS= read -r -d '' directory; do
    chmod 755 "$ROOT/${directory#./}"
done < <(cd /source/overlay && find . -type d -print0)
while IFS= read -r -d '' file; do
    chmod 644 "$ROOT/${file#./}"
done < <(cd /source/overlay && find . -type f -print0)
chmod 755 "$ROOT/usr/lib/mnos/first-boot" "$ROOT/usr/lib/mnos/wallpaper" \
    "$ROOT/usr/bin/mnos-center" "$ROOT/usr/bin/mnos-security-check" "$ROOT/usr/bin/mnos-help" \
    "$ROOT/usr/bin/mnos-net" "$ROOT/usr/sbin/mnos-network-policy" "$ROOT/usr/bin/mnos-tools"
chmod 755 "$ROOT/usr/lib/mnos/installer" "$ROOT/usr/lib/mnos/start-installer" "$ROOT/usr/lib/mnos/install-system" "$ROOT/usr/lib/mnos/installer-session"
chmod 755 "$ROOT/usr/lib/mnos/account-preflight"
if [ -f "$ROOT/usr/share/applications/com.microsoft.VSCode.desktop" ]; then
    sed -i 's/value="code.desktop"/value="com.microsoft.VSCode.desktop"/' "$ROOT/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml"
fi
chmod 440 "$ROOT/etc/sudoers.d/50-mnos"
chmod 644 "$ROOT/usr/share/backgrounds/mnos/default.svg"
printf '%s\n' "${SOURCE_COMMIT:?}" > "$ROOT/etc/mnos/source-commit"
if [ "$ARCH" = amd64 ]; then
    grub-file --is-x86-multiboot2 /native/build/mnos.elf
    cp /native/build/mnos.elf "$ISO/boot/mnos.elf"
    cp /native/build/mnos.elf "$ROOT/boot/mnos.elf"
else
    cp /native/build/arm64/BOOTAA64.EFI "$ISO/boot/mnos-arm64.efi"
    cp /native/build/arm64/BOOTAA64.EFI "$ROOT/boot/mnos-arm64.efi"
fi
rm -f "$ROOT/etc/grub.d/09_mnos_native"
install -m 755 /source/overlay/etc/grub.d/41_mnos_native "$ROOT/etc/grub.d/41_mnos_native"
sed -i -E '/^userAutoLogin:/d; s/^doAutologin:.*/doAutologin: false/; s/^setRootPassword:.*/setRootPassword: false/; s/^allowWeakPasswords:.*/allowWeakPasswords: false/' "$ROOT/etc/calamares/modules/users.conf"
cp /source/overlay/usr/share/doc/mnos/guide.zh.txt "$ROOT/usr/share/doc/mnos/guide.zh.txt"
# Include the original kernel source and font license with the bootable binary.
mkdir -p "$ROOT/usr/src/mnos-native"
cp -a /native/src /native/include /native/tests /native/tools /native/arch /native/user /native/installer /native/Makefile /native/linker.ld /native/README.md /native/FILESYSTEM.md "$ROOT/usr/src/mnos-native/"
mkdir -p "$ROOT/usr/src/mnos-admin"
cp /source/src/kernel.c /source/src/kernel-admin.c /source/src/kernel-common.h "$ROOT/usr/src/mnos-admin/"
cp /source/src/net-admin.c "$ROOT/usr/src/mnos-admin/"
mkdir -p "$ROOT/usr/src/mnos-desktop"
cp /source/src/*.cpp /source/src/*.hpp /source/src/*.c /source/src/Makefile "$ROOT/usr/src/mnos-desktop/"
find "$ROOT/usr/src/mnos-desktop" -type d -exec chmod 755 {} +
find "$ROOT/usr/src/mnos-desktop" -type f -exec chmod 644 {} +
mkdir -p "$ROOT/usr/src/mnos-native/assets"
cp /native/assets/*.c /native/assets/*LICENSE.txt "$ROOT/usr/src/mnos-native/assets/"
find "$ROOT/usr/src/mnos-native" -type d -exec chmod 755 {} +
find "$ROOT/usr/src/mnos-native" "$ROOT/usr/src/mnos-admin" -type f -exec chmod 644 {} +
printf '127.0.0.1 localhost\n127.0.1.1 mnos-%s\n::1 localhost ip6-localhost ip6-loopback\n' "$PROFILE" > "$ROOT/etc/hosts"
printf 'mnos-%s\n' "$PROFILE" > "$ROOT/etc/hostname"
rm -f "$ROOT/etc/resolv.conf" "$ROOT/.dockerenv"
ln -s /run/NetworkManager/resolv.conf "$ROOT/etc/resolv.conf"
rm -rf "$ROOT/etc/apt/apt.conf.d/docker-"* "$ROOT/var/cache/apt/archives/"*.deb
kernel=$(find "$ROOT/boot" -maxdepth 1 -name 'vmlinuz-*' | sort -V | tail -1)
test -n "$kernel"
version=${kernel##*/vmlinuz-}
cp "$kernel" "$ISO/live/vmlinuz"
cp "$ROOT/boot/initrd.img-$version" "$ISO/live/initrd.img"
cp "$ROOT/usr/share/doc/mnos/packages.tsv" "/out/packages-$PROFILE-$ARCH.tsv"
cp "$ROOT/boot/config-$version" "/out/kernel-$PROFILE-$ARCH.config"
cp "$ROOT/etc/mnos/source-commit" "/out/source-$PROFILE-$ARCH.txt"
find "$ROOT/usr/lib" "$ROOT/boot" -type f -name '*.dtb' -printf '%P\n' | sort > "/out/device-trees-$PROFILE-$ARCH.txt"
# All package and overlay changes are complete. Avoid needlessly rebuilding
# ldconfig and journal catalogs during the first (possibly emulated) ISO boot.
chroot "$ROOT" /usr/lib/systemd/systemd-update-done
mksquashfs "$ROOT" "$ISO/live/filesystem.squashfs" -noappend -comp xz \
    "${BCJ[@]}" -b 1M -processors 4 -no-progress
(cd "$ISO/live" && sha256sum filesystem.squashfs > filesystem.sha256)
cp /usr/share/grub/unicode.pf2 "$ISO/boot/grub/unicode.pf2"
# Debian's signed EFI core does not embed the Multiboot2 loader. Keep modules
# from the SAME rootfs package version available when Secure Boot is off.
cp -a "$ROOT/usr/lib/grub/$EFI" "$ISO/boot/grub/"
cat > "$ISO/boot/grub/grub.cfg" <<EOF
set default=0
set timeout=10
if [ "\$shim_lock" != "y" ]; then
    insmod all_video
    if loadfont /boot/grub/unicode.pf2; then
        set gfxmode=1280x720,auto
        set gfxpayload=keep
        terminal_output gfxterm
    fi
fi
set menu_color_normal=light-gray/black
set menu_color_highlight=black/light-cyan
menuentry 'Install mnOS - $ARCH (virtual machines and hardware)' {
    linux /live/vmlinuz boot=live mnos.install=1 apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=$CONSOLE,115200n8 console=tty0
    initrd /live/initrd.img
}
EOF
cat >> "$ISO/boot/grub/grub.cfg" <<EOF
menuentry 'Install mnOS - software rendering (keeps display drivers)' {
    linux /live/vmlinuz boot=live mnos.install=1 mnos.graphics=compat apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=$CONSOLE,115200n8 console=tty0
    initrd /live/initrd.img
}
menuentry 'Try desktop / diagnostics (changes are temporary)' {
    linux /live/vmlinuz boot=live apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=tty0 console=$CONSOLE,115200n8
    initrd /live/initrd.img
}
EOF
if [ "$ARCH" = amd64 ]; then
cat >> "$ISO/boot/grub/grub.cfg" <<'EOF'
menuentry 'Experimental C kernel - not the installer - Secure Boot off' {
    insmod multiboot2
    multiboot2 /boot/mnos.elf
    boot
}
EOF
else
cat >> "$ISO/boot/grub/grub.cfg" <<'EOF'
menuentry 'Experimental C kernel - GOP/serial - Secure Boot off' {
    chainloader /boot/mnos-arm64.efi
    boot
}
EOF
fi
cat > /work/early.cfg <<'EOF'
search --file --set=root /live/filesystem.squashfs
set prefix=($root)/boot/grub
configfile $prefix/grub.cfg
EOF
if [ "$ARCH" = amd64 ]; then
grub-mkimage -O i386-pc -o /work/core.img -p /boot/grub -c /work/early.cfg \
    biosdisk iso9660 normal configfile search search_fs_file multiboot2 linux \
    all_video font gfxterm gettext serial terminal
cat /usr/lib/grub/i386-pc/cdboot.img /work/core.img > "$ISO/boot/grub/i386-pc/eltorito.img"
fi

# Debian's Microsoft-signed shim -> Debian-signed GRUB -> Debian-signed kernel.
# This is a standard signed chain, not a custom signing key or shim bypass.
truncate -s 16M /work/efi.img
mkfs.vfat /work/efi.img
mmd -i /work/efi.img ::/EFI ::/EFI/BOOT ::/EFI/debian
mcopy -i /work/efi.img "$ROOT/usr/lib/shim/shim$EFI_SUFFIX.efi.signed" "::/EFI/BOOT/BOOT${EFI_SUFFIX^^}.EFI"
mcopy -i /work/efi.img "$ROOT/usr/lib/grub/$EFI-signed/grub$EFI_SUFFIX.efi.signed" "::/EFI/BOOT/grub$EFI_SUFFIX.efi"
if [ -f "$ROOT/usr/lib/shim/mm$EFI_SUFFIX.efi" ]; then
    mcopy -i /work/efi.img "$ROOT/usr/lib/shim/mm$EFI_SUFFIX.efi" "::/EFI/BOOT/mm$EFI_SUFFIX.efi"
fi
mcopy -i /work/efi.img /work/early.cfg ::/EFI/BOOT/grub.cfg
mcopy -i /work/efi.img /work/early.cfg ::/EFI/debian/grub.cfg
# Optical boot can leave GRUB's root on the ISO rather than its appended ESP.
# Signed GRUB must find the same configuration through either device path.
mkdir -p "$ISO/EFI/BOOT" "$ISO/EFI/debian"
cp /work/early.cfg "$ISO/EFI/BOOT/grub.cfg"
cp /work/early.cfg "$ISO/EFI/debian/grub.cfg"

name="mnOS-2.0-preview-$PROFILE-$ARCH"
bios_args=()
if [ "$ARCH" = amd64 ]; then
bios_args=( \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img --protective-msdos-label \
    -partition_offset 16 -b boot/grub/i386-pc/eltorito.img -no-emul-boot \
    -boot-load-size 4 -boot-info-table --grub2-boot-info \
    -eltorito-alt-boot )
fi
xorriso -as mkisofs -r -J -joliet-long -V "MNOS_${PROFILE^^}" \
    "${bios_args[@]}" -e --interval:appended_partition_2:all:: -no-emul-boot \
    -append_partition 2 0xef /work/efi.img -appended_part_as_gpt \
    -o "/out/$name.iso" "$ISO"
cd /out
sha256sum "$name.iso" > "$name.iso.sha256"
chown "$BUILD_UID:$BUILD_GID" "$name.iso" "$name.iso.sha256" "packages-$PROFILE-$ARCH.tsv" \
    "kernel-$PROFILE-$ARCH.config" "source-$PROFILE-$ARCH.txt" "device-trees-$PROFILE-$ARCH.txt"
