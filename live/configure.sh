#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs inside the root filesystem image only, never on the host.
set -eu
case "${MNOS_PROFILE:?}" in hardware|qemu) ;; *) exit 2 ;; esac
test "${MNOS_BUILD_ROOT:-}" = 1 || { echo 'Run this script through live/build.sh.' >&2; exit 1; }

mkdir -p /etc/mnos /usr/share/doc/mnos /etc/xdg/xfce4/xfconf/xfce-perchannel-xml
printf '%s\n' "$MNOS_PROFILE" > /etc/mnos/profile
printf '%s\n' "${MNOS_SOURCE_COMMIT:-working-tree}" > /etc/mnos/source-commit
# Docker mounts hosts/hostname read-only while building; pack.sh writes them.
cat > /usr/lib/os-release <<'EOF'
PRETTY_NAME="mnOS Desktop 2.0 Preview"
NAME="mnOS Desktop"
VERSION_ID="2.0"
VERSION="2.0 Preview (Debian 13)"
VERSION_CODENAME=trixie
ID=mnos
ID_LIKE=debian
HOME_URL="https://github.com/zengyiqian2014-boop/mnOS"
SUPPORT_URL="https://github.com/zengyiqian2014-boop/mnOS/issues"
EOF
printf 'en_US.UTF-8 UTF-8\nzh_CN.UTF-8 UTF-8\n' > /etc/locale.gen
locale-gen
ln -snf /usr/share/zoneinfo/Etc/UTC /etc/localtime
passwd --lock root
update-cracklib
chmod 440 /etc/sudoers.d/50-mnos
chmod 755 /usr/lib/mnos/first-boot /usr/lib/mnos/wallpaper /usr/bin/mnos-center /usr/bin/mnos-security-check
chmod 755 /etc/grub.d/41_mnos_native
visudo -cf /etc/sudoers

sed -i 's/^ENABLED=.*/ENABLED=yes/' /etc/ufw/ufw.conf
sed -i 's/^DEFAULT_INPUT_POLICY=.*/DEFAULT_INPUT_POLICY="DROP"/' /etc/default/ufw
sed -i 's/^DEFAULT_FORWARD_POLICY=.*/DEFAULT_FORWARD_POLICY="DROP"/' /etc/default/ufw
sed -i 's/^max_log_file =.*/max_log_file = 20/' /etc/audit/auditd.conf
sed -i 's/^num_logs =.*/num_logs = 3/' /etc/audit/auditd.conf
# Service starts are suppressed by policy-rc.d during package installation.
systemctl enable NetworkManager lightdm apparmor auditd ufw chrony mnos-first-boot
systemctl enable apt-daily.timer apt-daily-upgrade.timer fstrim.timer
systemctl enable tuned
if [ "$MNOS_PROFILE" = qemu ]; then
    printf 'mnos-virtual\n' > /etc/tuned/active_profile
else
    printf 'mnos-desktop\n' > /etc/tuned/active_profile
fi
printf 'manual\n' > /etc/tuned/profile_mode
systemctl mask cockpit.socket
systemctl disable getty@tty1.service || true
mkdir -p /etc/systemd/system/getty@tty1.service.d
printf '[Unit]\nAfter=mnos-first-boot.service\n' > /etc/systemd/system/getty@tty1.service.d/10-enrollment.conf
systemctl set-default graphical.target

# Never preserve the build machine's network or block-device configuration.
printf '# Devices are discovered at boot.\n' > /etc/fstab
printf 'DEVICE partitions\nHOMEHOST <system>\n' > /etc/mdadm/mdadm.conf
rm -f /etc/NetworkManager/system-connections/* /etc/udev/rules.d/70-persistent-net.rules
printf 'RESUME=none\n' > /etc/initramfs-tools/conf.d/resume
printf 'COMPRESS=gzip\n' > /etc/initramfs-tools/conf.d/compression
printf 'GRUB_TIMEOUT=5\nGRUB_DISTRIBUTOR="mnOS"\nGRUB_CMDLINE_LINUX_DEFAULT="quiet apparmor=1 security=apparmor"\nGRUB_CMDLINE_LINUX=""\n' > /etc/default/grub

# Desktop defaults. Users can change these through the normal Xfce settings.
cp -a /usr/share/backgrounds/mnos/contour.svg /usr/share/backgrounds/mnos/default.svg
if [ "$MNOS_PROFILE" = qemu ]; then
    compositor=false
else
    compositor=true
fi
cat > /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfwm4.xml <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0">
 <property name="general" type="empty">
  <property name="theme" type="string" value="Arc-Dark"/>
  <property name="title_font" type="string" value="Inter Semi-Bold 10"/>
  <property name="button_layout" type="string" value="O|HMC"/>
  <property name="use_compositing" type="bool" value="$compositor"/>
  <property name="workspace_count" type="int" value="2"/>
 </property>
</channel>
EOF

# Keep the tested Debian installer modules, change only branding and constraints.
sed -i 's/^branding: debian/branding: mnos/; s/^prompt-install: false/prompt-install: true/' /etc/calamares/settings.conf
# The rootfs already has signed HTTPS repositories. No temporary CD apt source.
sed -i '/  - sources-media$/d; /  - sources-media-unmount$/d; /  - sources-final$/d' /etc/calamares/settings.conf
sed -i 's/minLength: -1.*/minLength: 12/; s/minlen=0/minlen=12/; s/minclass=0/minclass=2/' /etc/calamares/modules/users.conf
sed -i -E 's/^doAutologin:.*/doAutologin: false/; s/^setRootPassword:.*/setRootPassword: false/; s/^allowWeakPasswords:.*/allowWeakPasswords: false/' /etc/calamares/modules/users.conf
rm -f /etc/xdg/autostart/calamares-desktop-icon.desktop
mkdir -p /etc/calamares/branding/mnos
cp /etc/calamares/branding/debian/branding.desc /etc/calamares/branding/mnos/branding.desc
cp /etc/calamares/branding/debian/show.qml /etc/calamares/branding/mnos/show.qml
cp /etc/calamares/branding/debian/slide1.png /etc/calamares/branding/mnos/slide1.png
cp /usr/share/icons/hicolor/scalable/apps/mnos.svg /etc/calamares/branding/mnos/logo.svg
cp /usr/share/backgrounds/mnos/contour.svg /etc/calamares/branding/mnos/welcome.svg
sed -i 's/Debian GNU\/Linux/mnOS Desktop/g; s/Debian/mnOS/g; s/debian-logo.png/logo.svg/g; s/welcome.png/welcome.svg/g' /etc/calamares/branding/mnos/branding.desc
cat > /usr/share/applications/install-mnos.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Install mnOS
Name[zh_CN]=安装 mnOS
Exec=pkexec calamares
Icon=drive-harddisk
Categories=System;
EOF
printf '\nNoDisplay=true\n' >> /usr/share/applications/calamares-install-debian.desktop

dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\t${source:Package}\t${source:Version}\n' > /usr/share/doc/mnos/packages.tsv
uname -m > /usr/share/doc/mnos/build-architecture
update-initramfs -u -k all
glib-compile-schemas /usr/share/glib-2.0/schemas
update-desktop-database /usr/share/applications || true
gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
fc-cache -f
