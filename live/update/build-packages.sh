#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build in the matching Debian architecture container. No signing keys here.
set -euo pipefail
# Clamp archive metadata so a rerun cannot replace the same version with a
# different timestamp-only artifact. Source identity is published separately.
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
version=${1:?version}; compiled=${2:?compiled install tree}; out=${3:?output directory}
[[ "$version" =~ ^[0-9][a-zA-Z0-9.+~-]*$ ]] || exit 2
arch=$(dpkg --print-architecture)
case "$arch" in amd64|arm64) ;; *) exit 2 ;; esac
mkdir -p "$out"
work=$(mktemp -d)
control() {
    mkdir -p "$work/$1/DEBIAN"
    cat > "$work/$1/DEBIAN/control" <<EOF
Package: $1
Version: $version
Section: admin
Priority: optional
Architecture: $arch
Maintainer: mnOS <235238542+zengyiqian2014-boop@users.noreply.github.com>
Depends: $2
Description: $3
EOF
}
copy() { install -Dm"$3" "$compiled/$1" "$work/$2/$1"; }
control mnos-runtime 'libc6 (>= 2.41), libstdc++6, libgtk-3-0t64, libjson-glib-1.0-0, apt, curl, gpgv, ca-certificates, chrony, tzdata, locales, pkexec' 'mnOS compiled desktop and management utilities'
install -d "$work/mnos-runtime/usr/share/mnos"
{ printf 'mnos-auth\n'; sed '/^#/d; /^$/d' "$ROOT/packages/common.txt" "$ROOT/packages/ctf.txt" "$ROOT/packages/development.txt"; } \
    | sort -u > "$work/mnos-runtime/usr/share/mnos/full-packages.txt"
install -Dm644 "$ROOT/keys/mnos-archive.gpg" "$work/mnos-runtime/usr/share/keyrings/mnos-archive.gpg"
install -Dm644 "$ROOT/overlay/etc/apt/sources.list.d/mnos.sources" "$work/mnos-runtime/etc/apt/sources.list.d/mnos.sources"
install -Dm644 "$ROOT/keys/microsoft-code.asc" "$work/mnos-runtime/usr/share/keyrings/microsoft-code.asc"
printf 'deb [arch=%s signed-by=/usr/share/keyrings/microsoft-code.asc] https://packages.microsoft.com/repos/code stable main\n' "$arch" > "$work/mnos-runtime/etc/apt/sources.list.d/microsoft-code.list"
printf '/etc/apt/sources.list.d/mnos.sources\n/etc/apt/sources.list.d/microsoft-code.list\n' > "$work/mnos-runtime/DEBIAN/conffiles"
for file in mnos mnos-center mnos-help mnos-pkg mnos-admin mnos-security mnos-session mnos-region kernel; do copy "usr/bin/$file" mnos-runtime 755; done
for file in installer first-boot wallpaper configure-region; do copy "usr/lib/mnos/$file" mnos-runtime 755; done
install -Dm644 "$ROOT/overlay/usr/share/applications/mnos-region.desktop" "$work/mnos-runtime/usr/share/applications/mnos-region.desktop"
copy usr/sbin/mnos-network-policy mnos-runtime 755
for file in commands.en.txt commands.zh.txt guide.en.txt guide.zh.txt; do
    install -Dm644 "$ROOT/overlay/usr/share/doc/mnos/$file" "$work/mnos-runtime/usr/share/doc/mnos/$file"
done
for file in settings.json tools.json; do
    install -Dm644 "$ROOT/overlay/usr/share/mnos/$file" "$work/mnos-runtime/usr/share/mnos/$file"
done
for file in install-system installer-session account-preflight; do
    install -Dm755 "$ROOT/overlay/usr/lib/mnos/$file" "$work/mnos-runtime/usr/lib/mnos/$file"
done
install -Dm644 "$ROOT/overlay/etc/profile.d/mnos-help.sh" "$work/mnos-runtime/etc/profile.d/mnos-help.sh"
printf "/etc/profile.d/mnos-help.sh\n" >> "$work/mnos-runtime/DEBIAN/conffiles"
install -Dm644 "$ROOT/overlay/usr/share/bash-completion/completions/mnos-help" "$work/mnos-runtime/usr/share/bash-completion/completions/mnos-help"
if [ -f "$ROOT/../AUTHENTICATION.md" ]; then auth_doc="$ROOT/../AUTHENTICATION.md"; else auth_doc="$ROOT/build/AUTHENTICATION.md"; fi
install -Dm644 "$auth_doc" "$work/mnos-runtime/usr/share/doc/mnos/authentication.md"
vendor="$ROOT/build/security-src/vendor"
if [ ! -f "$vendor/README.md" ]; then vendor="$ROOT/../native/vendor"; fi
install -Dm644 "$vendor/README.md" "$work/mnos-runtime/usr/share/doc/mnos-runtime/THIRD_PARTY.md"
install -Dm644 "$vendor/monocypher/LICENCE.md" "$work/mnos-runtime/usr/share/doc/mnos-runtime/Monocypher-LICENCE.md"
install -Dm644 "$vendor/qrcodegen/qrcodegen.h" "$work/mnos-runtime/usr/share/doc/mnos-runtime/QR-Code-NOTICE.h"
dpkg-deb --root-owner-group --build "$work/mnos-runtime" "$out/mnos-runtime_${version}_${arch}.deb"

if [ -x "$compiled/usr/lib/mnos/login-helper" ]; then
    control mnos-auth "mnos-runtime (= $version), libpam0g, libopencv-core410, libopencv-imgproc410, libopencv-dnn410, libopencv-objdetect410, libopencv-calib3d410, libopencv-videoio410, opencv-data" 'mnOS optional password-first TOTP and additional face verification'
    copy usr/lib/mnos/login-helper mnos-auth 4755
    copy usr/lib/mnos/face-helper mnos-auth 4755
    copy usr/lib/mnos/face-camera mnos-auth 755
    copy usr/lib/mnos/login-process mnos-auth 755
    copy usr/bin/mnos-face-preview mnos-auth 755
    for module in pam_mnos_2fa.so pam_mnos_face.so; do
      pam=$(find "$compiled/usr/lib" -name "$module" -type f)
      [ -n "$pam" ]; copy "${pam#"$compiled/"}" mnos-auth 644
    done
    for file in etc/pam.d/mnos-security-setup etc/systemd/system/mnos-login-process.service \
      etc/systemd/system/mnos-login-process.socket etc/xdg/autostart/mnos-face-preview.desktop \
      usr/lib/sysusers.d/mnos-login.conf usr/share/pam-configs/mnos-2fa usr/share/pam-configs/mnos-face; do
        install -Dm644 "$ROOT/overlay/$file" "$work/mnos-auth/$file"
    done
    (cd "$ROOT/build/face-models" && sha256sum --check "$ROOT/face/SOURCES.sha256")
    install -d "$work/mnos-auth/usr/share/mnos/face"
    install -m644 "$ROOT/build/face-models/"* "$ROOT/face/SOURCES.sha256" "$work/mnos-auth/usr/share/mnos/face/"
    cat > "$work/mnos-auth/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
  systemd-sysusers /usr/lib/sysusers.d/mnos-login.conf
  install -d -o login-process -g login-process -m700 /var/lib/mnos-login-process
  pam-auth-update --package
  systemctl enable mnos-login-process.socket
  if [ -d /run/systemd/system ]; then systemctl daemon-reload; systemctl restart mnos-login-process.socket; fi
fi
EOF
    cat > "$work/mnos-auth/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = deconfigure ]; then
  if find /var/lib/mnos-login-process -maxdepth 2 \( -name '*.factor' -o -name '*.face' \) -print -quit 2>/dev/null | grep -q .; then
    echo 'Disable enrolled authenticators and face verification before removing authentication support.' >&2
    exit 1
  fi
  pam-auth-update --package --remove mnos-2fa mnos-face
  systemctl disable mnos-login-process.socket
  if [ -d /run/systemd/system ]; then systemctl stop mnos-login-process.socket mnos-login-process.service; fi
fi
EOF
    chmod 755 "$work/mnos-auth/DEBIAN/postinst" "$work/mnos-auth/DEBIAN/prerm"
    find "$work/mnos-auth/etc" -type f -printf '/etc/%P\n' > "$work/mnos-auth/DEBIAN/conffiles"
    dpkg-deb --root-owner-group --build "$work/mnos-auth" "$out/mnos-auth_${version}_${arch}.deb"

    # Feature pack uses APT for dependencies. The runtime is never a Python app;
    # Full deliberately restores upstream tools that may require Python.
    deps=$(sed '/^#/d; /^$/d' "$ROOT/packages/common.txt" "$ROOT/packages/ctf.txt" "$ROOT/packages/development.txt" | sort -u | paste -sd, -)
    control mnos-full "mnos-runtime (= $version), mnos-auth (= $version), $deps" 'mnOS full applications, development and authentication feature pack'
    install -d "$work/mnos-full/usr/share/mnos"
    printf 'full\n' > "$work/mnos-full/usr/share/mnos/flavor"
    dpkg-deb --root-owner-group --build "$work/mnos-full" "$out/mnos-full_${version}_${arch}.deb"
fi
printf 'Package scratch tree: %s\n' "$work"
