// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
namespace mnos {
inline void erase(std::string &s) {
    volatile char *p = s.empty() ? nullptr : &s[0];
    for (size_t i = 0; i < s.size(); ++i)
        p[i] = 0;
    s.clear();
}
// Fixed absolute executables and a clean environment. Secrets travel through stdin only.
inline int run(const std::vector<std::string> &args, const std::string &input = "") {
    if (args.empty() || args[0].empty() || args[0][0] != '/')
        throw std::runtime_error("Absolute executable required");
    int fds[2];
    if (pipe(fds))
        throw std::runtime_error("pipe failed");
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        throw std::runtime_error("fork failed");
    }
    if (!pid) {
        close(fds[1]);
        if (dup2(fds[0], STDIN_FILENO) < 0)
            _exit(126);
        close(fds[0]);
        std::vector<char *> argv;
        for (const auto &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin", lang[] = "LANG=C.UTF-8";
        char *env[] = {path, lang, nullptr};
        execve(argv[0], argv.data(), env);
        _exit(127);
    }
    close(fds[0]);
    size_t done = 0;
    while (done < input.size()) {
        ssize_t n = write(fds[1], input.data() + done, input.size() - done);
        if (n > 0)
            done += size_t(n);
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    close(fds[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            throw std::runtime_error("waitpid failed");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
inline void checked(const std::vector<std::string> &args, const std::string &input = "") {
    if (run(args, input))
        throw std::runtime_error("Command failed: " + args[0]);
}
inline bool username(const std::string &s) {
    if (s.empty() || s.size() > 31 || s[0] < 'a' || s[0] > 'z')
        return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}
} // namespace mnos
