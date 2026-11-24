#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d); trap 'rm -rf -- "$work"' EXIT
jq -n '["2.2.0~preview2","2.2.0~preview10"] | [ .[] as $v |
  ["amd64","arm64"][] | {package:"mnos-runtime",version:$v,architecture:.}] +
  [{package:"mnos-runtime",version:"2.3.0",architecture:"amd64"},
   {package:"unrelated",version:"9.0",architecture:"arm64"}]' > "$work/packages.json"
select_version() { bash "$root/update/catalog-version.sh" "$work/packages.json" "$@"; }
test "$(select_version auto)" = '2.2.0~preview10'
test "$(select_version '2.2.0~preview2')" = '2.2.0~preview2'
if select_version '2.3.0' >/dev/null 2>&1; then exit 1; fi
printf '[]\n' > "$work/packages.json"
if select_version auto >/dev/null 2>&1; then exit 1; fi
jq -n '["amd64","arm64"] | map({package:"mnos-runtime",version:"2.0\nINJECT=value",architecture:.})' > "$work/packages.json"
if select_version auto >/dev/null 2>&1; then exit 1; fi
echo 'Catalog version PASS: Debian version ordering, both-architecture availability, explicit version, missing packages and invalid metadata'
