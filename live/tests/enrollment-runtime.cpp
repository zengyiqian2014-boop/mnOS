// SPDX-License-Identifier: GPL-3.0-or-later
// Destructive only to the unique test user inside a disposable rootfs container.
#include "../src/process.hpp"
#include <cassert>
#include <cerrno>
#include <crypt.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <shadow.h>
#include <string>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
struct Session {
    int fd = -1;
    pid_t pid = -1;
    std::string output;
    Session() {
        pid = forkpty(&fd, nullptr, nullptr, nullptr);
        assert(pid >= 0);
        if (!pid) {
            execl("/usr/lib/mnos/first-boot", "first-boot", nullptr);
            _exit(127);
        }
    }
    ~Session() {
        if (pid > 0) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        if (fd >= 0)
            close(fd);
    }
    void expect(const std::string &text) {
        size_t start = output.size();
        for (int i = 0; i < 300; ++i) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 100) > 0) {
                char buf[4096];
                ssize_t n = read(fd, buf, sizeof buf);
                if (n > 0)
                    output.append(buf, n);
                else
                    break;
            }
            if (output.find(text, start) != std::string::npos)
                return;
        }
        // Classify failures without ever logging captured credential input.
        std::string reason;
        for (const char *message : {"Please enter a password", "Setup interrupted",
                                    "Cannot hide password", "Passwords do not match", "Username [mnos]"})
            if (output.find(message, start) != std::string::npos)
                reason += std::string("; observed ") + message;
        throw std::runtime_error("Enrollment did not reach: " + text + reason);
    }
    void send(const std::string &s) {
        if (write(fd, s.data(), s.size()) != ssize_t(s.size()))
            throw std::runtime_error("PTY write failed");
    }
    void exited(int code) {
        int status;
        assert(waitpid(pid, &status, 0) == pid);
        pid = -1;
        assert(WIFEXITED(status) && WEXITSTATUS(status) == code);
    }
};
int main() {
    const char *marker = "/var/lib/mnos/setup-complete";
    const std::string long_name = "mnos_enroll_test_" + std::string(48, 'a');
    const char *name = long_name.c_str();
    assert(geteuid() == 0 && access(marker, F_OK) != 0 && getpwnam(name) == nullptr);
    try {
        {
            Session s;
            s.expect("Username [mnos]");
            s.send("\x04");
            s.exited(1);
            assert(access(marker, F_OK) != 0);
        }
        // An explicit short password must not be rejected by mnOS policy.
        std::string secret = "q7";
        {
            Session s;
            s.expect("Username [mnos]");
            s.send("bad;name\n");
            s.expect("Use lower-case");
            s.send("root\n");
            s.expect("此用户名已被系统占用，请填写一个其他的");
            s.send(std::string(name) + "\n");
            s.expect("Password:");
            s.send(secret + "\n");
            s.expect("Confirm password");
            s.send(secret + "\n");
            s.expect("Account ready");
            s.exited(0);
            assert(s.output.find(secret) == std::string::npos);
        }
        auto *pw = getpwnam(name);
        assert(pw && pw->pw_uid >= 1000);
        auto *sp = getspnam(name);
        assert(sp && sp->sp_pwdp[0] != '!');
        crypt_data data{};
        char *result = crypt_r(secret.c_str(), sp->sp_pwdp, &data);
        assert(result && strcmp(result, sp->sp_pwdp) == 0);
        mnos::erase(secret);
        struct stat st{};
        assert(!stat(marker, &st) && (st.st_mode & 0777) == 0600);
        assert(!mnos::run({"/usr/lib/mnos/first-boot"}));
        mnos::checked({"/usr/sbin/userdel", "--remove", name});
        assert(!unlink(marker));
        std::cout << "Compiled enrollment: cancellation, invalid username, hidden input, hashed "
                     "password, private marker and idempotence PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (getpwnam(name))
            mnos::run({"/usr/sbin/userdel", "--remove", name});
        unlink(marker);
        return 1;
    }
}
