#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline checks on generated local fixtures. Never scan/capture a network.
set -eu
for tool in wireshark ghex sqlitebrowser sqlite3 fls mmls icat pngcheck zbarimg \
    tcpflow ngrep fcrackzip pwn gdb gdb-multiarch checksec readelf objdump \
    sqlmap ffuf gobuster john hashcat yara exiftool steghide binwalk \
    tmux zsh awk sed find xargs tar cpio getfacl getfattr lsof tree whois; do
    command -v "$tool" >/dev/null
done
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT
mnos-center --list-commands > "$work/commands"
for tool in ls cp mv rm grep sed find systemctl code firefox-esr nmap ghex fls; do
    awk -F '\t' -v name="$tool" '$1==name {found=1} END {exit !found}' "$work/commands"
done
mnos-help -cn ctf | grep -q '离线'
mnos-help commands | grep -q 'installed'
printf 'int main(void){return 0;}\n' > "$work/program.c"
cc -g "$work/program.c" -o "$work/program"
readelf -h "$work/program" | grep -q ELF
objdump -d "$work/program" > "$work/disassembly"
test -s "$work/disassembly"
gdb -nx --batch -ex 'info files' "$work/program" | grep -q 'Symbols from'
printf 'rule local_fixture { strings: $a = "mnOS CTF fixture" condition: $a }\n' > "$work/local.yar"
printf 'mnOS CTF fixture\n' > "$work/sample"
yara "$work/local.yar" "$work/sample" | grep -q '^local_fixture '
sqlite3 "$work/evidence.db" 'CREATE TABLE evidence(value); INSERT INTO evidence VALUES("mnOS");'
test "$(sqlite3 "$work/evidence.db" 'SELECT value FROM evidence;')" = mnOS
# A real empty little-endian PCAP, v2.4, Ethernet link type.
printf '\324\303\262\241\002\000\004\000\000\000\000\000\000\000\000\000\377\377\000\000\001\000\000\000' > "$work/sample.pcap"
tshark -r "$work/sample.pcap" > "$work/packets"
test ! -s "$work/packets"
fls -V
pngcheck -h > "$work/png-help" 2>&1 || test -s "$work/png-help"
printf 'CTF OFFLINE PASS: executable inventory, ELF analysis, debugger, YARA, SQLite and PCAP\n'
