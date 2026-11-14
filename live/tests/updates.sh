#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Real APT transactions against a disposable, locally signed fixture repository.
set -euo pipefail
[ "${MNOS_UPDATE_TEST_CONTAINER:-}" = 1 ] && [ "$(id -u)" = 0 ] && [ -f /.dockerenv ] || exit 2
cd /source/live
c++ -std=c++17 -O2 -Wall -Wextra -Werror -DMNOS_TEST_CATALOG_URL='"file:///tmp/update-repo/catalog.json.asc"' src/mnos.cpp $(pkg-config --cflags --libs json-glib-1.0) -o /tmp/mnos
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
sign() {
    bash update/repository.sh /tmp/update-repo "$key" >/dev/null
    local latest
    latest=$(find /tmp/update-repo -name 'mnos-runtime_*_all.deb' -printf '%f\n' | sort -V | tail -1)
    latest=${latest#mnos-runtime_}; latest=${latest%_all.deb}
    printf '{"schema":1,"latest_linux_runtime":"%s","resources":[{"kind":"patch","edition":"linux","architecture":"all","version":"%s","name":"mnos-runtime","url":"https://example.invalid/mnos-runtime.deb"}]}' "$latest" "$latest" > /tmp/update-repo/catalog.json
    gpg --batch --yes --local-user "$key" --clearsign -o /tmp/update-repo/catalog.json.asc /tmp/update-repo/catalog.json
}
fixture mnos-runtime 1.0; sign
apt-get update >/dev/null
apt-get install -y mnos-runtime >/dev/null
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 1.0
fixture mnos-runtime 2.0; sign
/tmp/mnos list patch > /tmp/update-list.log
grep -q 'mnos-runtime' /tmp/update-list.log
/tmp/mnos packages installed | grep -q 'mnos-runtime'
printf 'Y\n' | /tmp/mnos update >/tmp/update-forward.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
sed 's/"latest_linux_runtime":"2.0"/"latest_linux_runtime":"1.0"/' /tmp/update-repo/catalog.json > /tmp/older-catalog.json
gpg --batch --yes --local-user "$key" --clearsign -o /tmp/update-repo/catalog.json.asc /tmp/older-catalog.json
/tmp/mnos update >/tmp/update-stale-catalog.log 2>&1
grep -q 'will not downgrade' /tmp/update-stale-catalog.log
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
sign
printf 'Y\n' | /tmp/mnos rollback 1.0 >/tmp/update-rollback.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 1.0
fixture mnos-auth 2.0 'mnos-runtime (= 2.0)'
fixture mnos-full 2.0 'mnos-runtime (= 2.0), mnos-auth (= 2.0)'
fixture mnos-feature 2.0 'mnos-runtime (= 2.0)'
fixture mnos-excluded 1.0
fixture mnos-requires-excluded 1.0 'mnos-excluded'
fixture mnos-selected 1.0
sign
install -d /usr/share/mnos
printf 'mnos-feature\nmnos-auth\n' > /usr/share/mnos/full-packages.txt
printf 'Y\n' | /tmp/mnos full --without mnos-auth >/tmp/update-selected-full.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-feature)" = 2.0
test ! -e /usr/share/mnos/test/mnos-auth
test ! -e /usr/share/mnos/test/mnos-full
printf 'Y\n' | /tmp/mnos apt install mnos-selected mnos-excluded --without mnos-excluded >/tmp/update-without.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
test ! -e /usr/share/mnos/test/mnos-excluded
# A requested dependency cannot silently reinstall an excluded package.
if printf 'Y\n' | /tmp/mnos apt install mnos-requires-excluded --without mnos-excluded >/tmp/update-without-dependency.log 2>&1; then exit 1; fi
test ! -e /usr/share/mnos/test/mnos-requires-excluded
test ! -e /usr/share/mnos/test/mnos-excluded
# An already installed exclusion is kept; this option must never mean removal.
/tmp/mnos apt install mnos-selected --without mnos-selected >/tmp/update-without-existing.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
# Existing administrator pins continue to apply to other packages.
printf 'Package: mnos-excluded\nPin: version *\nPin-Priority: 1001\n\nPackage: mnos-selected\nPin: version 2.0\nPin-Priority: -1\n' > /etc/apt/preferences
fixture mnos-selected 2.0; sign; apt-get update >/dev/null
printf 'Y\n' | /tmp/mnos apt install mnos-selected --without mnos-excluded >/tmp/update-without-admin.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
if printf 'Y\n' | /tmp/mnos apt install mnos-requires-excluded --without mnos-excluded >/tmp/update-without-admin-conflict.log 2>&1; then exit 1; fi
test ! -e /usr/share/mnos/test/mnos-excluded
# Resolve an administrator-selected main preferences file as well.
mv /etc/apt/preferences /etc/apt/mnos-test-preferences
printf 'Dir::Etc::preferences "mnos-test-preferences";\n' > /etc/apt/apt.conf.d/99-mnos-test-preferences
printf 'Y\n' | /tmp/mnos apt install mnos-selected --without mnos-excluded >/tmp/update-without-custom-path.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
printf 'Y\n' | /tmp/mnos apt install mnos-selected —without mnos-excluded >/tmp/update-without-mobile-dash.log 2>&1
test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
if /tmp/mnos full --without >/dev/null 2>&1; then exit 1; fi
if /tmp/mnos apt install mnos-selected --without --allow-unauthenticated >/dev/null 2>&1; then exit 1; fi
if /tmp/mnos apt install mnos-selected --without 'a*' >/dev/null 2>&1; then exit 1; fi
if runuser -u nobody -- /tmp/mnos apt install mnos-selected --without mnos-excluded >/dev/null 2>&1; then exit 1; fi
test -z "$(find /tmp -maxdepth 1 -type d -name 'mnos-apt.*' -print -quit)"
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
printf 'tampered\n' > /tmp/update-repo/catalog.json.asc
if /tmp/mnos update >/tmp/update-catalog-signature.log 2>&1; then exit 1; fi
grep -q 'Resource index signature failed' /tmp/update-catalog-signature.log
test "$(cat /usr/share/mnos/test/mnos-runtime)" = 2.0
echo 'PASS updates: real signed APT refresh/install/update/version rollback/full-feature transaction; --without selection, dependency refusal, existing packages and administrator pins; ordinary-user denial, invalid version, tampered payload and invalid signature rejected'
