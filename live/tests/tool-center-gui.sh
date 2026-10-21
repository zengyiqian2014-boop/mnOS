#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
export LANG=C.UTF-8 LC_ALL=C.UTF-8
test "$(id -u)" -ne 0
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT
mkdir -p "$HOME/.cache"
mnos-center --self-test
mnos-tools --self-test
# A renderer screenshot, not a designer's mockup; keep alongside boot evidence.
cp /evidence/window-capture "$work/capture"
window_ready() {
    count=0
    while [ "$count" -lt 60 ]; do
        for id in $(xdotool search --onlyvisible --name "$1" 2>/dev/null || true); do
            if xdotool getwindowgeometry --shell "$id" | awk -F= '/^WIDTH=/{w=$2} /^HEIGHT=/{h=$2} END{exit !(w>=400 && h>=200)}'; then
                xdotool mousemove 0 0
                sleep 2
                return 0
            fi
        done
        count=$((count+1))
        sleep 1
    done
    echo "No visible application window: $1" >&2
    return 1
}
mnos-center --ctf > "$work/center.log" 2>&1 &
center=$!
window_ready '^mnOS'
"$work/capture" "$id" /evidence/ctf-workbench.png
# Typing a newly indexed command must filter live, without launching it.
xdotool type --window "$id" --clearmodifiers 'ls'
sleep 1
"$work/capture" "$id" /evidence/command-search.png
kill "$center"
wait "$center" || true
# Launch actual graphical analysis tools with local fixtures, without root.
printf 'mnOS CTF local fixture\n' > "$work/fixture.bin"
ghex "$work/fixture.bin" > /evidence/ghex.log 2>&1 &
hex=$!
window_ready 'fixture.bin'
"$work/capture" "$id" /evidence/ghex.png
kill "$hex"
wait "$hex" || true
sqlite3 "$work/fixture.db" 'CREATE TABLE evidence(value); INSERT INTO evidence VALUES("mnOS");'
sqlitebrowser "$work/fixture.db" > /evidence/sqlitebrowser.log 2>&1 &
db=$!
window_ready 'DB Browser for SQLite'
"$work/capture" "$id" /evidence/sqlitebrowser.png
kill "$db"
wait "$db" || true
wireshark > /evidence/wireshark.log 2>&1 &
capture=$!
window_ready 'Wireshark'
"$work/capture" "$id" /evidence/wireshark.png
kill "$capture"
wait "$capture" || true
echo 'GUI CTF PASS: ordinary-user command search, GHex, SQLite Browser and Wireshark windows'
