#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
ROOT=$(CDPATH= cd "$(dirname "$0")" && pwd)
OUT=${1:?usage: desktop/build.sh OUTPUT}
mkdir -p "$(dirname "$OUT")"
"${CC:-cc}" -std=c11 -Os -Wall -Wextra -Werror -static -s \
    "$ROOT/desktop.c" -o "$OUT"
