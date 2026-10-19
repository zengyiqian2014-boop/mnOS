#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
mkdir -p build/desktop
make host-tests | tee build/desktop/host-tests.log
installer/build.sh | tee build/desktop/build.log
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/desktop_vm.cpp -o build/desktop-vm
for arch in amd64 arm64; do
  build/desktop-vm build/desktop "$arch" 2>&1 | tee "build/desktop/validation-$arch.log"
done
