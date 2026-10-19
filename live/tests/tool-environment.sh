#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run against the completed Linux rootfs. Read-only package queries; no scans.
set -eu
for program in firefox-esr code nano cc c++ make cmake ninja pkg-config clangd gdb \
    apt apt-get apt-cache dpkg dpkg-query man mnos-help mnos-pkg nmap tcpdump; do
    command -v "$program" >/dev/null
done
mnos-help --list | grep -qx packages
mnos-help -cn packages | grep -q '依赖'
mnos-help code | grep -q 'sandbox'
mnos-pkg show libc6 >/dev/null || test -s /usr/share/doc/mnos/packages.tsv
mnos-pkg check
# Never let a wrapper accept options or local scripts as package names.
if mnos-pkg install --allow-unauthenticated >/dev/null 2>&1; then exit 1; fi
if mnos-pkg install ./script >/dev/null 2>&1; then exit 1; fi
if command -v javac >/dev/null; then echo 'Unexpected Java SDK' >&2; exit 1; fi
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT
printf '#include <stdio.h>\nint main(void){puts("C toolchain PASS");}\n' > "$work/hello.c"
cc -Wall -Wextra -Werror "$work/hello.c" -o "$work/c-program"
"$work/c-program"
printf '#include <iostream>\nint main(){std::cout << "C++ toolchain PASS" << std::endl;}\n' > "$work/hello.cpp"
c++ -std=c++20 -Wall -Wextra -Werror "$work/hello.cpp" -o "$work/cpp-program"
"$work/cpp-program"
# Bash's manual integration retains builtins and routes external tools to man.
bash -c '. /etc/profile.d/mnos-help.sh; help cd' | grep -q 'directory'
bash -c '. /etc/profile.d/mnos-help.sh; help -cn code' | grep -q '普通账号'
echo 'TOOL ENVIRONMENT PASS: browser/editor commands, C/C++ builds, manuals and package validation'
