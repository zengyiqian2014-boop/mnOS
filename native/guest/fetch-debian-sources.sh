#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch matching source archives for every shipped Debian binary package.
set -euo pipefail
ROOT=$(cd -- "$(dirname "$0")/.." && pwd)
MANIFEST=$ROOT/build/guest/DEBIAN-PACKAGES.tsv
OUT=$ROOT/build/debian-sources
STATE=$ROOT/build/debian-source-apt
test -s "$MANIFEST"
mkdir -p "$OUT" "$STATE/lists/partial" "$STATE/cache/archives/partial"
cat > "$STATE/sources.list" <<'EOF'
deb-src [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] http://deb.debian.org/debian trixie main
deb-src [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] http://deb.debian.org/debian trixie-updates main
deb-src [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] http://deb.debian.org/debian-security trixie-security main
EOF
opts=(-o "Dir::Etc::sourcelist=$STATE/sources.list" -o Dir::Etc::sourceparts=-
      -o "Dir::State::lists=$STATE/lists" -o "Dir::Cache=$STATE/cache"
      -o APT::Sandbox::User=root -o APT::Update::Error-Mode=any)
apt-get "${opts[@]}" update
cut -f3,4 "$MANIFEST" | LC_ALL=C sort -u > "$STATE/versions.tsv"
cd "$OUT"
while IFS=$'\t' read -r package version; do
    test -n "$package" && test -n "$version"
    apt-get "${opts[@]}" --download-only --only-source source "$package=$version"
done < "$STATE/versions.tsv"
cp "$STATE/versions.tsv" SOURCE-VERSIONS.tsv
find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS
sha256sum -c SHA256SUMS
