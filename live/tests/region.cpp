// SPDX-License-Identifier: GPL-3.0-or-later
#include "region.hpp"
#include "mnos-locale.h"
#include <cassert>
#include <iostream>
static bool accepts(const region::Config &c) {
    try { region::validate(c); return true; } catch (const std::exception &) {return false;}
}
int main() {
    region::Config c;
    for (auto language : {"en_US.UTF-8", "zh_CN.UTF-8"}) {
        c.language = language;
        for (auto zone : {"Etc/UTC", "Asia/Shanghai", "America/New_York", "Europe/London"}) {
            c.timezone = zone;
            assert(accepts(c));
            std::istringstream encoded(region::serialize(c));
            auto decoded = region::parse(encoded);
            assert(decoded.language == c.language && decoded.timezone == c.timezone && decoded.servers == c.servers);
        }
    }
    for (const auto &p : region::presets) { c.servers = {p.host}; assert(accepts(c)); }
    for (auto bad : {"", "pool.ntp.org\nmakestep 9 9", "-host", "host-", "bad..name", "a/b", "$(id)", "a;id", "a b", "[::1]"}) {
        c.servers = {bad}; assert(!accepts(c));
    }
    c.servers = {"192.0.2.1", "2001:db8::1", "ntp.example.org"}; assert(accepts(c));
    c.servers = {"TIME.GOOGLE.COM", "pool.ntp.org"}; assert(!accepts(c));
    c.servers = {"time.google.com", "time2.google.com"}; assert(accepts(c));
    c.servers = {"pool.ntp.org"};
    for (auto zone : {"../etc/passwd", "/etc/localtime", "Etc/../UTC", "zone.tab", "localtime", "Invalid/Zone"}) {
        c.timezone = zone; assert(!accepts(c));
    }
    c.timezone = "Etc/UTC"; c.language = "en_US.UTF-8\nEVIL=yes"; assert(!accepts(c));
    unsetenv("LC_ALL"); unsetenv("LC_MESSAGES");
    setenv("LANG", "zh_CN.UTF-8", 1); assert(mnos_chinese());
    setenv("LC_MESSAGES", "en_US.UTF-8", 1); assert(!mnos_chinese());
    setenv("LC_ALL", "zh_CN.UTF-8", 1); assert(mnos_chinese());
    setenv("LC_ALL", "C", 1); assert(!mnos_chinese());
    std::cout << "PASS: locale precedence, timezone database, server presets, IPv4/IPv6, input rejection, smear policy, round trip\n";
}
