#!/bin/bash
# Run as root in a disposable build container. The helper itself changes only
# a private temporary prefix; no disk devices or real accounts are created.
set -euo pipefail
helper=${1:?Path to account-preflight}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
sha256sum /etc/passwd /etc/shadow /etc/group /etc/gshadow > "$work/before"
account=mnos_credential_test_$(printf '%080d' 0)
check_pass() {
    printf '%s\n%s\n' "$account" "$1" | bash "$helper" --check >"$work/out" 2>"$work/err"
    test ! -s "$work/out" && test ! -s "$work/err"
}
check_pass q7
check_pass "$(printf '%0511d' 0)"
if printf '%s\n%s\n' "$account" "$(printf '%0512d' 0)" |
    bash "$helper" --check >"$work/out" 2>"$work/err"; then
    echo 'FAIL: libxcrypt overflow unexpectedly accepted' >&2; exit 1
fi
test ! -s "$work/out"
grep -q '未截断或修改磁盘' "$work/err"
for reserved in root daemon nobody; do
    if printf '%s\n' "$reserved" | bash "$helper" --name >"$work/out" 2>"$work/err"; then exit 1; fi
    grep -qx '此用户名已被系统占用，请填写一个其他的' "$work/err"
done
for invalid in 'bad;name' '-option' '../path'; do
    if printf '%s\n' "$invalid" | bash "$helper" --name >"$work/out" 2>"$work/err"; then exit 1; fi
done
if printf '%s\n\n' "$account" | bash "$helper" --check >"$work/out" 2>"$work/err"; then exit 1; fi
hash=$(printf '%s\nq7\n' "$account" | bash "$helper" --hash)
[[ "$hash" == '$y$'* ]]
sha256sum -c "$work/before" >/dev/null
printf '%s\n' 'PASS: long account, short/511-byte passwords, backend overflow rejection, occupied names, invalid input, and unchanged system accounts'
