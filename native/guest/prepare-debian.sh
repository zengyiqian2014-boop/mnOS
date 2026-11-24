#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
umask 022
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
SYS=$(cd -- "${1:?Usage: prepare-debian.sh DEBIAN_ROOT}" && pwd)
test -s "$SYS/etc/debian_version"
test -s "$ROOT/build/guest/busybox"
for app in mnos-apps mnos-gui mnos-pkg mnos-window-wait; do test -x "$SYS/usr/local/bin/$app"; done
mkdir -p "$SYS"/{usr/local/bin,usr/local/sbin,usr/local/lib/mnos,etc/X11/xinit,root,run,tmp}
install -m755 "$ROOT/build/guest/busybox" "$SYS/usr/local/lib/mnos/busybox"
rm -f "$SYS/usr/local/bin/mnos-busybox"
install -m755 "$ROOT/guest/debian-init" "$SYS/sbin/mnos-init"
install -m755 "$ROOT/guest/debian-help" "$SYS/usr/local/bin/help"
install -m755 "$ROOT/guest/dhcp" "$SYS/usr/local/lib/mnos-dhcp"
install -m644 "$ROOT/guest/xorg.conf" "$SYS/etc/X11/xorg.conf"
install -m755 "$ROOT/guest/desktop-session" "$SYS/usr/local/lib/mnos/desktop-session"
cat > "$SYS/etc/mnos-shellrc" <<'EOF'
export PS1='linux-guest# ' TERM=dumb
help() { /usr/local/bin/help "$@"; }
EOF
cat > "$SYS/root/.xinitrc" <<'EOF'
#!/bin/sh
exec /usr/local/lib/mnos/desktop-session
EOF
chmod 755 "$SYS/root/.xinitrc"
cat > "$SYS/root/.twmrc" <<'EOF'
NoGrabServer
RandomPlacement
BorderWidth 2
TitleFont "fixed"
MenuFont "fixed"
IconFont "fixed"
ResizeFont "fixed"
Color {
    BorderColor "#458f98"
    TitleBackground "#172732"
    TitleForeground "#e4f0f1"
}
Button1 = : title : f.move
Button1 = : frame : f.resize
Button3 = : title : f.delete
EOF
cat > "$SYS/etc/X11/xinit/xserverrc" <<'EOF'
#!/bin/sh
exec /usr/bin/X -nolisten tcp -noreset "$@"
EOF
chmod 755 "$SYS/etc/X11/xinit/xserverrc"
cat > "$SYS/usr/local/sbin/poweroff" <<'EOF'
#!/bin/sh
sync
exec /usr/local/lib/mnos/busybox poweroff -f
EOF
chmod 755 "$SYS/usr/local/sbin/poweroff"
chmod 1777 "$SYS/tmp"
printf 'mnos-linux-guest\n' > "$SYS/etc/hostname"
printf '127.0.0.1 localhost\n127.0.1.1 mnos-linux-guest\n::1 localhost\n' > "$SYS/etc/hosts"
: > "$SYS/etc/machine-id"
rm -f "$SYS/var/lib/dbus/machine-id"
# DHCP supplies real DNS; do not ship the builder's resolvers or trust roots.
: > "$SYS/etc/resolv.conf"
chmod 644 "$SYS/etc/"{mnos-shellrc,hostname,hosts,machine-id,resolv.conf} "$SYS/root/.twmrc"
find "$SYS/var/cache/apt/archives" -maxdepth 1 -type f -name '*.deb' -delete
find "$SYS/var/log" -type f -exec truncate -s 0 {} +
mkdir -p "$ROOT/build/guest"
# SquashFS stores directory timestamps at whole-second precision. Fontconfig
# cache v9 also checks nanoseconds; caching before this normalization makes the
# image rebuild every font cache on its first interpreted desktop launch.
# Both supported builders and the riscv64 guest use the same le64 cache format.
while IFS= read -r -d '' font_dir; do
    touch -m -d "@$(stat -c %Y "$font_dir")" "$font_dir"
done < <(find "$SYS/usr/share/fonts" -type d -print0)
fc-cache --sysroot "$SYS" -s -f
# GdkPixbuf needs the MIME cache even for GTK's own embedded PNG icons. This
# portable cache must exist in the offline image, independent of bootstrap
# trigger behavior under the build-only userspace emulator.
update-mime-database "$SYS/usr/share/mime"
test -s "$SYS/usr/share/mime/mime.cache"
mksquashfs "$SYS" "$ROOT/build/guest/rootfs.squashfs" -noappend -comp lz4 -Xhc -processors "${JOBS:-4}"
sha256sum "$ROOT/build/guest/rootfs.squashfs"
bash "$ROOT/guest/verify-image.sh"
