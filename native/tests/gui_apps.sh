#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
umask 022
root=$(cd -- "$(dirname "$0")/../.." && pwd)
if [ -z "${DISPLAY:-}" ];then exec xvfb-run -a bash "$0";fi
work=$(mktemp -d)
export MNOS_GUI_DIRECTORY=$work/socket-dir MNOS_GUI_NO_START=1
export XDG_DATA_HOME=$work/data XDG_DATA_DIRS=/usr/local/share:/usr/share HOME=$work/home LC_ALL=C
mkdir -p "$HOME"
server=
cleanup() { if [ -n "$server" ];then kill "$server" 2>/dev/null || :;wait "$server" 2>/dev/null || :;fi; }
trap cleanup EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$root/live/src" "$root/native/guest/app-center.cpp" $(pkg-config --cflags --libs gtk+-3.0 gio-unix-2.0) -o "$work/mnos-apps"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$root/native/guest/gui-client.cpp" -o "$work/mnos-gui"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$root/native/guest/window-wait.cpp" $(pkg-config --cflags --libs x11) -o "$work/mnos-window-wait"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$root/native/tests/window_wait.cpp" $(pkg-config --cflags --libs x11) -o "$work/window-wait-test"
"$work/window-wait-test" "$work/mnos-window-wait"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$root/native/tests/gui_apps.cpp" $(pkg-config --cflags --libs gtk+-3.0 x11) -o "$work/gui-test"
"$work/mnos-apps" --serve > "$work/server.log" 2>&1 & server=$!
for attempt in {1..100};do
    if [ -S "$MNOS_GUI_DIRECTORY/socket" ];then break;fi
    sleep .05
done
"$work/gui-test" "$work" "$work/mnos-gui" || { cat "$work/server.log";exit 1; }
