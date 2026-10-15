#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Stage release assets without dropping firmware to fit GitHub's per-file limit.
set -euo pipefail
SOURCE=$(realpath "${1:?Usage: release-assets.sh ARTIFACT_DIRECTORY}")
DEST="$SOURCE/publish"
LIMIT=${MNOS_RELEASE_MAX_BYTES:-2147483647}
CHUNK=${MNOS_RELEASE_CHUNK_BYTES:-1992294400}
[[ "$LIMIT" =~ ^[1-9][0-9]*$ && "$CHUNK" =~ ^[1-9][0-9]*$ ]] || exit 2
(( CHUNK <= LIMIT )) || exit 2
test ! -e "$DEST" || { echo 'Publish directory already exists; refusing stale assets.' >&2; exit 1; }
(cd "$SOURCE" && sha256sum --check -- ./*.iso.sha256)
mkdir "$DEST"
split_images=0
for path in "$SOURCE"/*; do
    test -f "$path" || continue
    name=${path##*/}
    if [[ "$name" = *.iso ]] && (( $(stat -c %s "$path") > LIMIT )); then
        split -b "$CHUNK" -d -a 2 -- "$path" "$DEST/$name.part"
        (cd "$DEST" && sha256sum -- "$name".part[0-9][0-9]) > "$DEST/$name.parts.sha256"
        split_images=1
    else
        cp -l -- "$path" "$DEST/$name"
    fi
done
if [ "$split_images" = 1 ]; then
cat > "$DEST/READ-SPLIT-ISOS.txt" <<'EOF'
Some complete hardware ISOs exceed GitHub's 2 GiB per-file limit.
They are split for transport only; the bootable ISO itself is unchanged.
Download ALL .partNN files for ONE image, its .parts.sha256 and its .iso.sha256.
Linux: substitute the exact ISO name below (including its architecture).

sha256sum --check IMAGE.iso.parts.sha256
cat IMAGE.iso.part[0-9][0-9] > IMAGE.iso
sha256sum --check IMAGE.iso.sha256

Windows Command Prompt (for two parts):
copy /b IMAGE.iso.part00+IMAGE.iso.part01 IMAGE.iso
certutil -hashfile IMAGE.iso SHA256

Compare the resulting SHA256 with IMAGE.iso.sha256 before writing to USB.
Do not write individual parts to USB or boot a part file in QEMU.

完整硬件镜像超过 GitHub 单文件 2 GiB 限制时，仅为传输拆分，不裁掉驱动。
下载同一个镜像的全部 partNN 文件，按编号合并为 ISO，再核对完整 ISO 的
SHA256。不要直接烧录分片。小于限制的版本仍直接提供完整 .iso 文件。
EOF
fi
