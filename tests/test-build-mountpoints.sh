#!/bin/sh
# Check the archive made from a checkout with no untracked mount directories.
set -eu

ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
GEN_INIT_CPIO=${GEN_INIT_CPIO:-${KERNEL_SRC:-$HOME/kernel/linux-6.13.3}/usr/gen_init_cpio}
[ -x "$GEN_INIT_CPIO" ] || { echo "set GEN_INIT_CPIO to the kernel's gen_init_cpio" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/rootfs/boot" "$WORK/tools" "$WORK/unpack"
cp "$ROOT/build.sh" "$WORK/rootfs/build.sh"
printf 'test kernel\n' > "$WORK/rootfs/boot/bzImage"
# ISO generation is unrelated to this check; use the real archive tools.
printf '#!/bin/sh\n: > "$2"\n' > "$WORK/tools/grub-mkrescue"
chmod +x "$WORK/tools/grub-mkrescue"
PATH="$WORK/tools:$PATH" GEN_INIT_CPIO="$GEN_INIT_CPIO" OUT="$WORK/test.iso" \
    sh "$WORK/rootfs/build.sh" > "$WORK/build.log"

gzip -dc "$WORK/rootfs/boot/initramfs.cpio.gz" > "$WORK/rootfs.cpio"
(cd "$WORK/unpack"; cpio -idm --quiet --no-preserve-owner proc sys < "$WORK/rootfs.cpio")
for dir in proc sys; do
    [ -d "$WORK/unpack/$dir" ] || { echo "missing /$dir in generated initramfs" >&2; exit 1; }
    [ ! -e "$WORK/rootfs/$dir" ] || { echo "build unexpectedly changed the source tree" >&2; exit 1; }
done
echo 'mount directories are present in a clean build'
