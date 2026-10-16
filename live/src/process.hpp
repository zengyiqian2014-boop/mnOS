// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
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
    if (pipe2(fds, O_CLOEXEC))
        throw std::runtime_error("pipe failed");
    // Keep the pipe away from standard descriptors, even with stdin closed.
    for (int &fd : fds) {
        if (fd > STDERR_FILENO)
            continue;
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        if (moved < 0) {
            close(fds[0]);
            close(fds[1]);
            throw std::runtime_error("Cannot reserve child input descriptor");
        }
        close(fd);
        fd = moved;
    }
    // Construct arguments before spawning. No C++ allocation or library state
    // is touched in a fork child after NSS/password libraries have been used.
    std::vector<char *> argv;
    for (const auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin", lang[] = "LANG=C.UTF-8";
    char *env[] = {path, lang, nullptr};
    posix_spawn_file_actions_t actions;
    pid_t pid = -1;
    int error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
        if (!error)
            error = posix_spawn_file_actions_addclose(&actions, fds[0]);
        if (!error)
            error = posix_spawn_file_actions_addclose(&actions, fds[1]);
        if (!error)
            error = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), env);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(fds[0]);
    if (error) {
        close(fds[1]);
        throw std::runtime_error(std::string("Cannot start ") + args[0] + ": " + strerror(error));
    }
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
    return done == input.size() && WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
inline void checked(const std::vector<std::string> &args, const std::string &input = "") {
    if (run(args, input))
        throw std::runtime_error("Command failed: " + args[0]);
}
inline bool username(const std::string &s) {
    if (s.empty() || s[0] < 'a' || s[0] > 'z')
        return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}
} // namespace mnos
