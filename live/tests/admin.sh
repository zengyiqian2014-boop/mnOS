#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
bin=${1:-/usr/bin/mnos-admin}
"$bin" info | grep -F 'mnOS Linux Edition'
"$bin" help -cn | grep -F '系统管理'
"$bin" accounts | grep '^root:' >/dev/null
for name in --no-pager 'ssh.service;id' 'bad/name.service' 'ssh'; do
    if "$bin" services status "$name" >/dev/null 2>&1; then
        echo 'Rejected unit accepted' >&2;exit 1
    fi
done
if [ "$(id -u)" -eq 0 ]; then
    if runuser -u nobody -- "$bin" services restart not-a-real-mnos-unit.service >/tmp/mnos-admin-denial 2>&1; then exit 1; fi
    grep -F 'requires administrator authentication' /tmp/mnos-admin-denial
fi
echo 'ADMIN PASS: ordinary queries, bilingual help, option injection rejection and no automatic elevation'
