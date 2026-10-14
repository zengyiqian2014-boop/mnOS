#!/bin/sh
# Start an mnOS ISO in QEMU.
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

usage() {
    echo "usage: $0 [--serial] [--memory MiB] [ISO]"
}
fail() {
    echo "error: $*" >&2
    exit 1
}

ROOT=$(CDPATH= cd "$(dirname "$0")" && pwd)
VERSION=$(sed -n 's/^VERSION=//p' "$ROOT/etc/os-release" | tr -d '"')
ISO=
MEMORY=1024
SERIAL=0
QEMU=${QEMU:-qemu-system-x86_64}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --serial) SERIAL=1 ;;
        --memory)
            [ "$#" -ge 2 ] || fail '--memory needs a value in MiB'
            MEMORY=$2
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        --)
            shift
            [ "$#" -le 1 ] || fail 'expected one ISO path'
            if [ "$#" -eq 1 ]; then
                [ -z "$ISO" ] || fail 'expected one ISO path'
                ISO=$1
            fi
            break
            ;;
        -*) fail "unknown option: $1" ;;
        *)
            [ -z "$ISO" ] || fail 'expected one ISO path'
            ISO=$1
            ;;
    esac
    shift
done
case "$MEMORY" in
    ''|*[!0-9]*|0*) fail 'memory must be a positive number of MiB' ;;
esac
ISO=${ISO:-$(dirname "$ROOT")/mnOSv${VERSION}.iso}
[ -r "$ISO" ] && [ -f "$ISO" ] || fail "ISO not found: $ISO (run ./build.sh or pass an ISO path)"
command -v "$QEMU" >/dev/null 2>&1 || fail "QEMU not found: $QEMU"

ACCEL=tcg
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    ACCEL=kvm
fi
set -- -accel "$ACCEL" -m "$MEMORY" -cdrom "$ISO"
if [ "$SERIAL" -eq 1 ]; then
    set -- "$@" -nographic
fi
exec "$QEMU" "$@"
