#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
make -j4 all user-programs host-tools
make -f arch/arm64/Makefile -j4 all user-programs build/arm64/BOOTAA64-TEST.EFI
make host-tests | tee build/process-host-tests.log
OUT="$PWD/build" sh pack.sh demo
OUT="$PWD/build" sh pack.sh test
sh arch/arm64/pack.sh demo
sh arch/arm64/pack.sh test
bash tests/exec_boot.sh | tee build/process-boot-tests.log
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/process_vm.cpp -o build/process-vm
build/process-vm build x86_64
build/process-vm build arm64
