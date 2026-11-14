// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace region {
struct Preset { const char *name, *host; bool smear; const char *chinese; };
inline constexpr Preset presets[] = {
    {"NTP Pool", "pool.ntp.org", false, "NTP Pool"},
    {"Aliyun", "ntp.aliyun.com", false, "阿里云"},
    {"Tencent", "ntp.tencent.com", false, "腾讯云"},
    {"Cloudflare", "time.cloudflare.com", false, "Cloudflare"},
    {"Google (leap smear)", "time.google.com", true, "Google（平滑闰秒）"},
};
struct Config {
    std::string language = "en_US.UTF-8", formats = "en_US.UTF-8", timezone = "Etc/UTC";
    std::vector<std::string> servers = {"pool.ntp.org"};
};
inline bool locale(const std::string &s) { return s == "en_US.UTF-8" || s == "zh_CN.UTF-8"; }
inline bool hostname(const std::string &s) {
    if (s.empty() || s.size() > 253) return false;
    unsigned char bytes[16];
    if (inet_pton(AF_INET, s.c_str(), bytes) == 1 || inet_pton(AF_INET6, s.c_str(), bytes) == 1) return true;
    size_t label = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '.') {
            if (!label || s[i - 1] == '-') return false;
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || (c == '-' && label))) return false;
            if (++label > 63) return false;
        }
    }
    return label && s.back() != '-';
}
inline bool zone_name(const std::string &s) {
    if (s.empty() || s.size() > 128 || s[0] == '/' || s.find("..") != std::string::npos ||
        s.find("//") != std::string::npos) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) return false;
    return s != "localtime" && s != "posixrules" && s.rfind("posix/", 0) != 0 && s.rfind("right/", 0) != 0;
}
inline void validate(const Config &c, const std::filesystem::path &zones = "/usr/share/zoneinfo") {
    if (!locale(c.language) || !locale(c.formats)) throw std::runtime_error("Choose English or Simplified Chinese / 请选择英文或简体中文");
    if (!zone_name(c.timezone)) throw std::runtime_error("Invalid time zone / 时区无效");
    std::error_code ec;
    auto base = std::filesystem::canonical(zones, ec);
    if (ec) throw std::runtime_error("Time zone database unavailable / 缺少时区数据库");
    auto file = std::filesystem::canonical(base / c.timezone, ec);
    if (ec || file.string().rfind(base.string() + '/', 0) != 0 || !std::filesystem::is_regular_file(file))
        throw std::runtime_error("Unknown time zone / 未找到此时区");
    std::ifstream input(file, std::ios::binary);
    char magic[4]{}; input.read(magic, 4);
    if (std::string(magic, 4) != "TZif") throw std::runtime_error("Not a time zone file / 无效时区文件");
    if (c.servers.empty() || c.servers.size() > 16) throw std::runtime_error("Choose 1–16 time servers / 请选择 1–16 个时间服务器");
    bool google = false, other = false;
    for (auto s : c.servers) {
        if (!hostname(s)) throw std::runtime_error("Invalid time server address / 时间服务器地址无效");
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char v) { return char(v >= 'A' && v <= 'Z' ? v + 32 : v); });
        if (s == "time.google.com" || s == "time1.google.com" || s == "time2.google.com" ||
            s == "time3.google.com" || s == "time4.google.com") google = true;
        else other = true;
    }
    if (google && other) throw std::runtime_error("Use Google smear servers separately / Google 平滑闰秒服务器需单独使用");
}
inline std::string serialize(const Config &c) {
    std::string text = c.language + '\n' + c.formats + '\n' + c.timezone + '\n';
    for (const auto &s : c.servers) text += s + '\n';
    return text;
}
inline Config parse(std::istream &input) {
    Config c;
    if (!std::getline(input, c.language) || !std::getline(input, c.formats) || !std::getline(input, c.timezone))
        throw std::runtime_error("Incomplete regional settings / 地区设置不完整");
    c.servers.clear(); std::string line;
    while (std::getline(input, line)) {
        if (c.servers.size() == 16 || line.size() > 253) throw std::runtime_error("Invalid server list / 服务器列表无效");
        if (!line.empty()) c.servers.push_back(line);
    }
    return c;
}
inline std::string locale_file(const Config &c) {
    return "LANG=" + c.language + "\nLANGUAGE=" + (c.language == "zh_CN.UTF-8" ? "zh_CN:zh" : "en_US:en") +
        "\nLC_TIME=" + c.formats + "\nLC_NUMERIC=" + c.formats + "\nLC_MONETARY=" + c.formats +
        "\nLC_PAPER=" + c.formats + "\nLC_MEASUREMENT=" + c.formats + '\n';
}
inline std::string chrony_file(const Config &c) {
    std::string out = "# Managed by mnOS Language, Region & Time\n";
    for (const auto &server : c.servers) out += "pool " + server + " iburst maxsources 4\n";
    return out + "driftfile /var/lib/chrony/chrony.drift\nrtcsync\nmakestep 1 3\n"
        "keyfile /etc/chrony/chrony.keys\nlogdir /var/log/chrony\n";
}
}
