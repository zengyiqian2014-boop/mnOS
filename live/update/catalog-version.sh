#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Input is the package/version JSON parsed from the verified APT Packages file.
set -euo pipefail
packages=${1:?verified package metadata}; requested=${2:-auto}
versions=$(jq -er '[.[] | select(.package == "mnos-runtime" and
  (.architecture == "amd64" or .architecture == "arm64"))] |
  group_by(.version)[] | select(any(.[]; .architecture == "amd64") and
  any(.[]; .architecture == "arm64")) | .[0].version | strings' "$packages") || {
  echo 'No signed runtime version is published for both architectures.' >&2; exit 1;
}
latest=''; found=false
while IFS= read -r version; do
  [[ "$version" =~ ^[0-9][a-zA-Z0-9.+~-]*$ ]] || {
    echo 'Invalid runtime version in signed package metadata.' >&2; exit 1;
  }
  if [ -z "$latest" ] || dpkg --compare-versions "$version" gt "$latest"; then latest=$version; fi
  if [ "$version" = "$requested" ]; then found=true; fi
done <<< "$versions"
if [ "$requested" = auto ]; then printf '%s\n' "$latest"
elif "$found"; then printf '%s\n' "$requested"
else echo 'Requested runtime version is not published for both architectures.' >&2; exit 1
fi
