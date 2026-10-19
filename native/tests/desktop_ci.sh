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
# The desktop changes focus after an authentication boundary. Exercise the
# existing process/isolation and ARM console suites before publishing too.
bash tests/process_ci.sh 2>&1 | tee build/desktop/process-regression.log
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/arm64_console.cpp -o build/arm64/console-test
build/arm64/console-test build/arm64 2>&1 | tee build/desktop/arm64-console-regression.log
