#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Flat, signed APT repository; retain previous .deb versions for rollback.
set -euo pipefail
repo=${1:?directory}; key=${2:?signing key fingerprint}
cd "$repo"
test -n "$(find . -maxdepth 1 -name '*.deb' -print -quit)"
apt-ftparchive packages . > Packages
gzip -n -k -f Packages
apt-ftparchive -o APT::FTPArchive::Release::Origin=mnOS \
  -o APT::FTPArchive::Release::Label=mnOS \
  -o APT::FTPArchive::Release::Suite=stable \
  -o APT::FTPArchive::Release::Architectures='amd64 arm64' release . > Release
gpg --batch --yes --local-user "$key" --clearsign --digest-algo SHA256 -o InRelease Release
gpg --batch --yes --local-user "$key" --detach-sign --digest-algo SHA256 -o Release.gpg Release
sha256sum ./*.deb > SHA256SUMS
