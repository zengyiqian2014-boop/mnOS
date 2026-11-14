#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
destination="$root/build/face-models"
mkdir -p "$destination"
commit=48b10fa505aeb51fbd127fc60a5c660040e823b7
if (cd "$destination" && sha256sum --check --status "$root/face/SOURCES.sha256"); then
    chmod 644 "$destination/yunet.onnx" "$destination/sface.onnx" "$destination/YUNET-LICENSE" "$destination/SFACE-LICENSE"
    exit 0
fi
for spec in \
    'yunet.onnx face_detection_yunet/face_detection_yunet_2023mar.onnx media' \
    'sface.onnx face_recognition_sface/face_recognition_sface_2021dec.onnx media' \
    'YUNET-LICENSE face_detection_yunet/LICENSE raw' \
    'SFACE-LICENSE face_recognition_sface/LICENSE raw'; do
    read -r name path host <<< "$spec"
    case "$host" in
        media) url="https://media.githubusercontent.com/media/opencv/opencv_zoo/$commit/models/$path" ;;
        raw) url="https://raw.githubusercontent.com/opencv/opencv_zoo/$commit/models/$path" ;;
    esac
    curl --fail --show-error --location --retry 3 --proto '=https' "$url" -o "$destination/$name.part"
    mv "$destination/$name.part" "$destination/$name"
done
(cd "$destination" && sha256sum --check "$root/face/SOURCES.sha256")
chmod 644 "$destination/yunet.onnx" "$destination/sface.onnx" "$destination/YUNET-LICENSE" "$destination/SFACE-LICENSE"
