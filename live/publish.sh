#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Upload checked artifacts to a draft; publish only once every upload succeeds.
set -euo pipefail
ARTIFACTS=$(realpath "${1:?Artifact directory required}")
TAG=${2:?Release tag required}
SOURCE_SHA=${3:?Verified source commit required}
: "${GH_REPO:?}" "${GH_TOKEN:?}"
[[ "$TAG" =~ ^v2\.0-desktop-preview\.[0-9]+$ ]] || exit 2
[[ "$SOURCE_SHA" =~ ^[0-9a-f]{40}$ ]] || exit 2
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
for arch in amd64 arm64; do
    for profile in qemu hardware; do
        test "$(cat "$ARTIFACTS/source-$profile-$arch.txt")" = "$SOURCE_SHA"
        test -s "$ARTIFACTS/mnOS-2.0-preview-$profile-$arch.iso"
        test -s "$ARTIFACTS/mnOS-2.0-preview-$profile-$arch.iso.sha256"
        test -s "$ARTIFACTS/packages-$profile-$arch.tsv"
        test -s "$ARTIFACTS/kernel-$profile-$arch.config"
    done
done
test -s "$ARTIFACTS/native-demo.iso"
test -s "$ARTIFACTS/native-demo.iso.sha256"
bash "$ROOT/release-assets.sh" "$ARTIFACTS"
TEMP=$(mktemp -d)
trap 'rm -rf -- "$TEMP"' EXIT
if gh api "repos/$GH_REPO/git/ref/tags/$TAG" > "$TEMP/ref.json" 2>/dev/null; then
    jq -e --arg sha "$SOURCE_SHA" '.object.type == "commit" and .object.sha == $sha' "$TEMP/ref.json"
else
    gh api --method POST "repos/$GH_REPO/git/refs" -f "ref=refs/tags/$TAG" -f "sha=$SOURCE_SHA" >/dev/null
fi
if gh release view "$TAG" --json isDraft --jq .isDraft > "$TEMP/state" 2>/dev/null; then
    test "$(cat "$TEMP/state")" = true || { echo 'Refusing to overwrite a published release.' >&2; exit 1; }
else
    gh release create "$TAG" --draft --prerelease --verify-tag \
        --title "mnOS Desktop $TAG" --notes-file "$ROOT/RELEASE.md"
fi
while IFS= read -r -d '' asset; do
    uploaded=false
    for attempt in 1 2 3; do
        if gh release upload "$TAG" "$asset" --clobber; then uploaded=true; break; fi
        sleep 3
    done
    "$uploaded" || { echo "Upload failed: ${asset##*/}" >&2; exit 1; }
done < <(find "$ARTIFACTS/publish" -maxdepth 1 -type f -print0 | sort -z)
gh release view "$TAG" --json assets > "$TEMP/assets.json"
while IFS= read -r -d '' asset; do
    jq -e --arg name "${asset##*/}" --argjson size "$(stat -c %s "$asset")" \
        'any(.assets[]; .name == $name and .size == $size and .state == "uploaded")' "$TEMP/assets.json" >/dev/null
done < <(find "$ARTIFACTS/publish" -maxdepth 1 -type f -print0)
gh release edit "$TAG" --notes-file "$ROOT/RELEASE.md" --draft=false
