// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/command-index.hpp"
#include <cassert>
#include <fstream>
#include <sys/stat.h>
int main() {
    char temp[] = "/tmp/mnos-command-index.XXXXXX";
    assert(mkdtemp(temp));
    const std::filesystem::path root(temp), first=root/"first", second=root/"second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    for (const auto &path : {first/"sample", second/"sample", first/"with'quote;name", first/"not-executable"}) {
        std::ofstream(path) << "#!/bin/sh\ntouch '" << (root/"should-not-exist").string() << "'\n";
        assert(chmod(path.c_str(), path.filename()=="not-executable" ? 0600 : 0700)==0);
    }
    std::filesystem::create_directory(first/"directory");
    std::filesystem::create_symlink(first/"sample", first/"alias");
    std::filesystem::create_symlink(root/"absent", first/"broken");
    auto result = command_index({(root/"missing").string(), first.string(), second.string()});
    assert(result.size()==3);
    assert(result.at("sample")==(first/"sample").string());
    assert(result.at("alias")==(first/"alias").string());
    assert(result.count("with'quote;name"));
    assert(!std::filesystem::exists(root/"should-not-exist"));
    std::filesystem::remove_all(root);
}
