// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

// Inventory only: never execute a discovered file, including with --help.
// A few utilities interpret unknown flags as an operation, not documentation.
inline std::map<std::string, std::string> command_index(const std::vector<std::string> &dirs) {
    std::map<std::string, std::string> result;
    for (const auto &dir : dirs) {
        std::error_code error;
        std::filesystem::directory_iterator it(dir, error), end;
        for (; !error && it != end; it.increment(error)) {
            const auto path = it->path();
            const auto name = path.filename().string();
            if (name.empty() || name.front() == '-' || name.find_first_of("\r\n\t") != std::string::npos)
                continue;
            std::error_code stat_error;
            if (it->is_regular_file(stat_error) && !stat_error && access(path.c_str(), X_OK) == 0)
                result.emplace(name, path.string());
        }
    }
    return result;
}
inline std::map<std::string, std::string> installed_commands() {
    return command_index({"/usr/local/bin", "/usr/bin", "/bin", "/usr/local/sbin", "/usr/sbin", "/sbin"});
}
