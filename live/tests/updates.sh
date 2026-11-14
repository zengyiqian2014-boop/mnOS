#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Real APT transactions against a disposable, locally signed fixture repository.
set -euo pipefail
[ "${MNOS_UPDATE_TEST_CONTAINER:-}" = 1 ] && [ "$(id -u)" = 0 ] && [ -f /.dockerenv ] || exit 2
cd /source/live
c++ -std=c++17 -O2 -Wall -Wextra -Werror src/mnos.cpp -o /tmp/mnos
export GNUPGHOME=/tmp/update-test-keys
install -d -m700 "$GNUPGHOME"
gpg --batch --passphrase '' --quick-generate-key 'Disposable mnOS update test' ed25519 sign 1d >/dev/null 2>&1
key=$(gpg --with-colons --list-keys 2>/dev/null | awk -F: '$1=="fpr"{print $10;exit}')
gpg --export "$key" > /usr/share/keyrings/mnos-archive.gpg
mkdir -p /tmp/update-repo
chmod 755 /tmp/update-repo
mv /etc/apt/sources.list.d/debian.sources /tmp/original-debian.sources
cat > /etc/apt/sources.list.d/mnos.sources <<'EOF'
Types: deb
URIs: file:/tmp/update-repo
Suites: ./
Signed-By: /usr/share/keyrings/mnos-archive.gpg
EOF
fixture() {
    local package=$1 version=$2 depends=${3:-}
    local dir=/tmp/fixture-$package-$version
    mkdir -p "$dir/DEBIAN" "$dir/usr/share/mnos/test"
    cat > "$dir/DEBIAN/control" <<EOF
Package: $package
Version: $version
Architecture: all
Maintainer: disposable-test <test@example.invalid>
Description: Disposable update test fixture
EOF
    if [ -n "$depends" ]; then printf 'Depends: %s\n' "$depends" >> "$dir/DEBIAN/control"; fi
    printf '%s\n' "$version" > "$dir/usr/share/mnos/test/$package"
    dpkg-deb --root-owner-group --build "$dir" "/tmp/update-repo/${package}_${version}_all.deb" >/dev/null
}
sign() { bash update/repository.sh /tmp/update-repo "$key" >/dev/null; }
fixture mnos-runtime 1.0; sign
apt-get update >/dev/null
apt-get install -y mnos-runtime >/dev/null
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 1.0
fixture mnos-runtime 2.0; sign
printf 'Y\n' | /tmp/mnos update >/tmp/update-forward.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
printf 'Y\n' | /tmp/mnos rollback 1.0 >/tmp/update-rollback.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 1.0
fixture mnos-auth 2.0 'mnos-runtime (= 2.0)'
fixture mnos-full 2.0 'mnos-runtime (= 2.0), mnos-auth (= 2.0)'; sign
printf 'Y\n' | /tmp/mnos full >/tmp/update-full.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-auth)" = 2.0
test "$(cat /usr/share/mnos/test/mnos-full)" = 2.0
if runuser -u nobody -- /tmp/mnos update >/dev/null 2>&1; then exit 1; fi
if /tmp/mnos rollback --allow-unauthenticated >/dev/null 2>&1; then exit 1; fi
# APT must reject damaged payloads before dpkg can change the installed version.
fixture mnos-runtime 3.0
fixture mnos-auth 3.0 'mnos-runtime (= 3.0)'
fixture mnos-full 3.0 'mnos-runtime (= 3.0), mnos-auth (= 3.0)'; sign
printf 'tampered' >> /tmp/update-repo/mnos-runtime_3.0_all.deb
if printf 'Y\n' | /tmp/mnos update >/tmp/update-tamper.log 2>&1; then exit 1; fi
grep -Eq 'Hash Sum mismatch|unexpected size' /tmp/update-tamper.log
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
printf 'tampered\n' > /tmp/update-repo/InRelease
if /tmp/mnos update >/tmp/update-signature.log 2>&1; then exit 1; fi
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
echo 'PASS updates: real signed APT refresh/install/update/version rollback/full-feature transaction; ordinary-user denial, invalid version, tampered payload and invalid signature rejected'
