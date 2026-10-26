#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
PROFILE=${1:-installer}
EDITION=${EDITION:-linux}
case "$EDITION" in linux|combined) ;; *) echo 'EDITION must be linux or combined.' >&2; exit 2 ;; esac
ARCH=${2:-$(uname -m)}
case "$ARCH" in x86_64) ARCH=amd64 ;; aarch64) ARCH=arm64 ;; amd64|arm64) ;; *) echo 'Unsupported architecture.' >&2; exit 2 ;; esac
case "$PROFILE" in installer|qemu|hardware) ;; *) echo "Usage: $0 [installer] [amd64|arm64]" >&2; exit 2 ;; esac
case "$(uname -m):$ARCH" in x86_64:amd64|aarch64:arm64) ;; *) echo 'Build on a matching native architecture runner.' >&2; exit 2 ;; esac
command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
if [ "$ARCH" = amd64 ]; then make -C "$ROOT/../native" -j"${JOBS:-4}" all host-tools; fi
if [ "$ARCH" = arm64 ]; then
    docker build -t mnos-native-arm-builder -f "$ROOT/../native/arch/arm64/Builder.Dockerfile" "$ROOT/../native"
    docker run --rm --network none -v "$ROOT/../native:/native" -w /native mnos-native-arm-builder \
        make -f arch/arm64/Makefile -j"${JOBS:-4}" all
fi
mkdir -p "$ROOT/build/native-src"
cp "$ROOT/../native/src/mnfs.c" "$ROOT/../native/include/mnfs.h" "$ROOT/build/native-src/"
cp "$ROOT/../native/tools/mnfs.c" "$ROOT/build/native-src/mnfs-tool.c"
OUT=${OUT:-$ROOT/out}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
SOURCE_COMMIT=$(git -C "$ROOT" rev-parse HEAD)
if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then SOURCE_COMMIT=$SOURCE_COMMIT-dirty; fi
IMAGE="mnos-live:$PROFILE-$ARCH"
ca_args=()
if [ -n "${MNOS_BUILD_CA_BUNDLE:-}" ]; then
    test -r "$MNOS_BUILD_CA_BUNDLE" || { echo 'CA bundle is not readable.' >&2; exit 2; }
    ca_args=(--secret "id=build_ca,src=$MNOS_BUILD_CA_BUNDLE")
fi
docker build --build-arg HTTP_PROXY --build-arg HTTPS_PROXY --build-arg NO_PROXY \
    "${ca_args[@]}" \
    --build-arg "PROFILE=$PROFILE" \
    -t "$IMAGE" -f "$ROOT/Rootfs.Dockerfile" "$ROOT"
docker build --build-arg HTTP_PROXY --build-arg HTTPS_PROXY --build-arg NO_PROXY \
    -t mnos-live-builder -f "$ROOT/Builder.Dockerfile" "$ROOT"
CONTAINER=$(docker create "$IMAGE")
trap 'docker rm -f "$CONTAINER" >/dev/null 2>&1 || true' EXIT
# Stream the rootfs. No host chroot, host service changes or rootfs tar copy.
docker export "$CONTAINER" | docker run --rm -i --network none \
    --memory=3g --cpus="${JOBS:-4}" \
    -e "PROFILE=$PROFILE" -e "EDITION=$EDITION" -e "ARCH=$ARCH" -e "SOURCE_COMMIT=$SOURCE_COMMIT" -e "BUILD_UID=$(id -u)" -e "BUILD_GID=$(id -g)" \
    -v "$OUT:/out" -v "$ROOT:/source:ro" -v "$ROOT/../native:/native:ro" mnos-live-builder \
    bash /source/pack.sh
echo "Built $OUT/mnOS-2.0-preview-$PROFILE-$ARCH.iso"
