#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Private build-container entry point. Rootfs tar comes from stdin.
set -euo pipefail
test -f /.dockerenv || exit 1
case "${PROFILE:?}" in qemu|hardware) ;; *) exit 2 ;; esac
mkdir -p /work/root /work/iso/{live,boot/grub/i386-pc}
tar --numeric-owner -xf - -C /work/root
ROOT=/work/root
ISO=/work/iso
# Workspace directories may be private (0700). Never transfer that mode to /usr.
while IFS= read -r -d '' directory; do
    chmod 755 "$ROOT/${directory#./}"
done < <(cd /source/overlay && find . -type d -print0)
printf '%s\n' "${SOURCE_COMMIT:?}" > "$ROOT/etc/mnos/source-commit"
grub-file --is-x86-multiboot2 /native/build/mnos.elf
cp /native/build/mnos.elf "$ISO/boot/mnos.elf"
cp /native/build/mnos.elf "$ROOT/boot/mnos.elf"
rm -f "$ROOT/etc/grub.d/09_mnos_native"
install -m 755 /source/overlay/etc/grub.d/41_mnos_native "$ROOT/etc/grub.d/41_mnos_native"
sed -i -E '/^userAutoLogin:/d; s/^doAutologin:.*/doAutologin: false/; s/^setRootPassword:.*/setRootPassword: false/; s/^allowWeakPasswords:.*/allowWeakPasswords: false/' "$ROOT/etc/calamares/modules/users.conf"
cp /source/overlay/usr/share/doc/mnos/guide.zh.txt "$ROOT/usr/share/doc/mnos/guide.zh.txt"
# Include the original kernel source and font license with the bootable binary.
mkdir -p "$ROOT/usr/src/mnos-native"
cp -a /native/src /native/include /native/tests /native/tools /native/Makefile /native/linker.ld /native/README.md "$ROOT/usr/src/mnos-native/"
mkdir -p "$ROOT/usr/src/mnos-native/assets"
cp /native/assets/font.c /native/assets/FONT-LICENSE.txt "$ROOT/usr/src/mnos-native/assets/"
find "$ROOT/usr/src/mnos-native" -type d -exec chmod 755 {} +
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
cp "$ROOT/usr/share/doc/mnos/packages.tsv" "/out/packages-$PROFILE.tsv"
cp "$ROOT/boot/config-$version" "/out/kernel-$PROFILE.config"
cp "$ROOT/etc/mnos/source-commit" "/out/source-$PROFILE.txt"
mksquashfs "$ROOT" "$ISO/live/filesystem.squashfs" -noappend -comp zstd \
    -Xcompression-level 8 -b 1M -processors 4 -no-progress
cp /usr/share/grub/unicode.pf2 "$ISO/boot/grub/unicode.pf2"
cat > "$ISO/boot/grub/grub.cfg" <<EOF
set default=0
set timeout=5
insmod all_video
if loadfont /boot/grub/unicode.pf2; then
    set gfxmode=1280x720,auto
    set gfxpayload=keep
    terminal_output gfxterm
fi
set menu_color_normal=light-gray/black
set menu_color_highlight=black/light-cyan
menuentry 'mnOS Desktop 2.0 - $PROFILE' {
    linux /live/vmlinuz boot=live quiet apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=tty0 console=ttyS0,115200n8
    initrd /live/initrd.img
}
menuentry 'mnOS native kernel 0.1 - Secure Boot off' {
    multiboot2 /boot/mnos.elf
    boot
}
menuentry 'mnOS Desktop 2.0 - safe graphics' {
    linux /live/vmlinuz boot=live nomodeset apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=tty0 console=ttyS0,115200n8
    initrd /live/initrd.img
}
menuentry 'mnOS Desktop 2.0 - boot messages' {
    linux /live/vmlinuz boot=live apparmor=1 security=apparmor audit=1 audit_backlog_limit=8192 console=tty0 console=ttyS0,115200n8
    initrd /live/initrd.img
}
EOF
cat > /work/early.cfg <<'EOF'
search --file --set=root /live/filesystem.squashfs
set prefix=($root)/boot/grub
configfile $prefix/grub.cfg
EOF
grub-mkimage -O i386-pc -o /work/core.img -p /boot/grub -c /work/early.cfg \
    biosdisk iso9660 normal configfile search search_fs_file multiboot2 linux \
    all_video font gfxterm gettext serial terminal
cat /usr/lib/grub/i386-pc/cdboot.img /work/core.img > "$ISO/boot/grub/i386-pc/eltorito.img"

# Debian's Microsoft-signed shim -> Debian-signed GRUB -> Debian-signed kernel.
# This is a standard signed chain, not a custom signing key or shim bypass.
truncate -s 16M /work/efi.img
mkfs.vfat /work/efi.img
mmd -i /work/efi.img ::/EFI ::/EFI/BOOT ::/EFI/debian
mcopy -i /work/efi.img "$ROOT/usr/lib/shim/shimx64.efi.signed" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i /work/efi.img "$ROOT/usr/lib/grub/x86_64-efi-signed/grubx64.efi.signed" ::/EFI/BOOT/grubx64.efi
if [ -f "$ROOT/usr/lib/shim/mmx64.efi" ]; then
    mcopy -i /work/efi.img "$ROOT/usr/lib/shim/mmx64.efi" ::/EFI/BOOT/mmx64.efi
fi
mcopy -i /work/efi.img /work/early.cfg ::/EFI/BOOT/grub.cfg
mcopy -i /work/efi.img /work/early.cfg ::/EFI/debian/grub.cfg

name="mnOS-2.0-preview-$PROFILE-amd64"
xorriso -as mkisofs -r -J -joliet-long -V "MNOS_${PROFILE^^}" \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img --protective-msdos-label \
    -partition_offset 16 -b boot/grub/i386-pc/eltorito.img -no-emul-boot \
    -boot-load-size 4 -boot-info-table --grub2-boot-info \
    -eltorito-alt-boot -e --interval:appended_partition_2:all:: -no-emul-boot \
    -append_partition 2 0xef /work/efi.img -appended_part_as_gpt \
    -o "/out/$name.iso" "$ISO"
cd /out
sha256sum "$name.iso" > "$name.iso.sha256"
chown "$BUILD_UID:$BUILD_GID" "$name.iso" "$name.iso.sha256" "packages-$PROFILE.tsv" \
    "kernel-$PROFILE.config" "source-$PROFILE.txt"
