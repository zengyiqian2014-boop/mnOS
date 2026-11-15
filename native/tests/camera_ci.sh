#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
mkdir -p build/desktop
make host-tests | tee build/desktop/camera-host-tests.log
GUEST=0 FLAVOR=full bash installer/build.sh > build/desktop/camera-build.log 2>&1
c++ -std=c++20 -O2 -Wall -Wextra -Werror tests/usb_camera.cpp -lusbredirparser -o build/usb-camera
c++ -std=c++20 -O2 -Wall -Wextra -Werror tests/camera_vm.cpp -o build/camera-vm
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/desktop_vm.cpp -o build/desktop-vm
for arch in amd64 arm64; do
  for transport in bulk iso; do
    build/camera-vm build/desktop "$arch" "$transport" 2>&1 | tee "build/desktop/camera-$arch-$transport-validation.log"
  done
  build/desktop-vm build/desktop "$arch" 2>&1 | tee "build/desktop/camera-$arch-desktop-regression.log"
done
cc -std=c11 -O2 -Wall -Wextra -Werror tools/screenshots.c -lz -o build/screenshots
build/screenshots build/desktop
