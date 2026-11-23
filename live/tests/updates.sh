#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Real APT transactions against a disposable, locally signed fixture repository.
set -euo pipefail
umask 022
[ "${MNOS_UPDATE_TEST_CONTAINER:-}" = 1 ] && [ "$(id -u)" = 0 ] && [ -f /.dockerenv ] || exit 2
cd /source/live
c++ -std=c++17 -O2 -Wall -Wextra -Werror -DMNOS_TEST_CATALOG_URL='"file:///tmp/update-repo/catalog.json.asc"' src/mnos.cpp $(pkg-config --cflags --libs json-glib-1.0) -o /tmp/mnos
c++ -std=c++17 -O2 -Wall -Wextra -Werror src/package.cpp -o /tmp/mnos-pkg
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/install-notice.cpp -o /tmp/mnos-install-notice-test
/tmp/mnos-install-notice-test
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
for protected in c cpp c++ C CPP cPP Cpp CpP C++ asm aSM ASM ASm gcc g++ gcc-14 binutils linux-libc-dev; do
    if /tmp/mnos --uninstall mnos-selected "$protected" > /tmp/update-protected.log 2>&1; then exit 1; fi
    grep -Fq "你的胆子也是肥嘟嘟的，居然敢卸载[$protected]" /tmp/update-protected.log
    test "$(cat /usr/share/mnos/test/mnos-selected)" = 1.0
done
if runuser -u nobody -- /tmp/mnos --uninstall mnos-selected >/dev/null 2>&1; then exit 1; fi
printf 'Y\n' | /tmp/mnos —uninstall mnos-selected >/tmp/update-uninstall.log 2>&1
test ! -e /usr/share/mnos/test/mnos-selected
printf 'Y\n' | /tmp/mnos apt install mnos-selected >/dev/null
# A dependency removal must not accidentally remove a compiler either.
fixture mnos-compiler-base 1.0
fixture gcc-99 1.0 'mnos-compiler-base'; sign; apt-get update >/dev/null
apt-get install -y gcc-99 >/dev/null
if /tmp/mnos --uninstall mnos-compiler-base >/tmp/update-uninstall-dependency.log 2>&1; then exit 1; fi
grep -Fq '居然敢卸载[gcc-99]' /tmp/update-uninstall-dependency.log
test -e /usr/share/mnos/test/mnos-compiler-base
test -e /usr/share/mnos/test/gcc-99
# An explicit runtime/header request has a separate default-no notice. Decline
# and EOF cannot start APT, and the notice must leave APT's own answer unread.
fixture python3-mnos-notice 1.0;sign;apt-get update >/dev/null
if /tmp/mnos apt install -y python3-mnos-notice </dev/null >/tmp/update-notice-eof.log 2>&1;then exit 1;fi
test ! -e /usr/share/mnos/test/python3-mnos-notice
grep -Fq 'Installation cancelled' /tmp/update-notice-eof.log
if printf 'n\n' | LC_ALL=zh_CN.UTF-8 /tmp/mnos-pkg install python3-mnos-notice >/tmp/update-notice-cn.log 2>&1;then exit 1;fi
grep -Fq '已取消安装' /tmp/update-notice-cn.log
test ! -e /usr/share/mnos/test/python3-mnos-notice
/tmp/mnos apt --simulate install python3-mnos-notice </dev/null >/tmp/update-notice-simulate.log 2>&1
if grep -Fq 'Continue installation?' /tmp/update-notice-simulate.log;then exit 1;fi
/tmp/mnos apt install python3-mnos-notice --without python3-mnos-notice </dev/null >/tmp/update-notice-excluded.log 2>&1
test ! -e /usr/share/mnos/test/python3-mnos-notice
printf 'y\nY\n' | /tmp/mnos apt install python3-mnos-notice >/tmp/update-notice-accepted.log 2>&1
test -e /usr/share/mnos/test/python3-mnos-notice
printf 'Y\n' | /tmp/mnos-pkg remove python3-mnos-notice >/dev/null
printf 'y\nY\n' | /tmp/mnos-pkg install python3-mnos-notice >/tmp/update-pkg-notice-accepted.log 2>&1
test -e /usr/share/mnos/test/python3-mnos-notice
if /tmp/mnos-pkg install linux-libc-dev </dev/null >/tmp/update-headers-notice.log 2>&1;then exit 1;fi
grep -Fq 'Headers are not loaded as runtime libraries' /tmp/update-headers-notice.log
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
echo 'PASS updates: signed Raw catalog, package listing, real APT updates/rollback; --without selection and dependency exclusion; uninstall, case-insensitive compiler aliases and dependency-removal protection; tampered metadata/payload rejected'
