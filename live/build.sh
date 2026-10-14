#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
PROFILE=${1:-qemu}
case "$PROFILE" in qemu|hardware) ;; *) echo "Usage: $0 [qemu|hardware]" >&2; exit 2 ;; esac
command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
make -C "$ROOT/../native" -j"${JOBS:-4}"
OUT=${OUT:-$ROOT/out}
mkdir -p "$ROOT/build-ca" "$OUT"
OUT=$(cd "$OUT" && pwd)
SOURCE_COMMIT=$(git -C "$ROOT" rev-parse HEAD)
if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then SOURCE_COMMIT=$SOURCE_COMMIT-dirty; fi
IMAGE="mnos-live:$PROFILE"
docker build --build-arg HTTP_PROXY --build-arg HTTPS_PROXY --build-arg NO_PROXY \
    --build-arg "PROFILE=$PROFILE" \
    -t "$IMAGE" -f "$ROOT/Rootfs.Dockerfile" "$ROOT"
docker build --build-arg HTTP_PROXY --build-arg HTTPS_PROXY --build-arg NO_PROXY \
    -t mnos-live-builder -f "$ROOT/Builder.Dockerfile" "$ROOT"
CONTAINER=$(docker create "$IMAGE")
trap 'docker rm -f "$CONTAINER" >/dev/null 2>&1 || true' EXIT
# Stream the rootfs. No host chroot, host service changes or rootfs tar copy.
docker export "$CONTAINER" | docker run --rm -i --network none \
    --memory=3g --cpus="${JOBS:-4}" \
    -e "PROFILE=$PROFILE" -e "SOURCE_COMMIT=$SOURCE_COMMIT" -e "BUILD_UID=$(id -u)" -e "BUILD_GID=$(id -g)" \
    -v "$OUT:/out" -v "$ROOT:/source:ro" -v "$ROOT/../native:/native:ro" mnos-live-builder \
    bash /source/pack.sh
echo "Built $OUT/mnOS-2.0-preview-$PROFILE-amd64.iso"
