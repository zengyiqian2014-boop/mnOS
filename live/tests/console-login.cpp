// SPDX-License-Identifier: GPL-3.0-or-later
// Run only as root inside a disposable container with the real login/PAM stack.
#include "../src/process.hpp"
#include <cassert>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
struct Console {
    int fd = -1;
    pid_t pid = -1;
    size_t cursor = 0;
    bool entered_input = false;
    std::string output;
    Console() {
        pid = forkpty(&fd, nullptr, nullptr, nullptr);
        assert(pid >= 0);
        if (!pid) {
            execl("/sbin/agetty", "agetty", "--skip-login", "--login-options", "--",
                  "--noreset", "--noclear", "-", "vt100", nullptr);
            _exit(127);
        }
    }
    ~Console() {
        if (pid > 0) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        if (fd >= 0) close(fd);
    }
    void expect(const std::string &value) {
        for (int i = 0; i < 300; ++i) {
            size_t at = output.find(value, cursor);
            if (at != std::string::npos) { cursor = at + value.size(); return; }
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 100) > 0) {
                char bytes[4096];
                ssize_t n = read(fd, bytes, sizeof bytes);
                if (n <= 0) break;
                output.append(bytes, n);
            }
        }
        // Startup diagnostics are safe only before any credentials are sent.
        throw std::runtime_error("Console did not reach expected authentication state: " + value +
                                 (entered_input ? "" : "; startup output: " + output));
    }
    void send(const std::string &value) {
        entered_input = true;
        if (write(fd, value.data(), value.size()) != ssize_t(value.size()))
            throw std::runtime_error("Console input failed");
    }
};
int main() {
    const std::string name = "mnos_console_test_" + std::string(46, 'a');
    assert(geteuid() == 0 && name.size() > 42 && !getpwnam(name.c_str()));
    try {
        mnos::checked({"/usr/sbin/useradd", "-M", "-l", "-s", "/bin/sh", name});
        mnos::checked({"/usr/sbin/chpasswd", "-c", "YESCRYPT"}, name + ":q7\n");
        {
            Console c;
            c.expect("login:"); c.send(name + "\n"); c.expect("Password:");
            c.send("deliberately-incorrect\n"); c.expect("Login incorrect");
            c.expect("login:"); c.send(name + "\n"); c.expect("Password:");
            c.send("q7\n");
            c.expect("$ ");
            // No automatic login: only the correct password may start a user shell.
            c.send("test \"$(id -un)\" = " + name +
                   " && test \"$(id -u)\" -ge 1000 && printf 'MNOS_%s\\n' CONSOLE_PASS\n");
            c.expect("\r\nMNOS_CONSOLE_PASS\r\n");
            assert(c.output.find("deliberately-incorrect") == std::string::npos);
            assert(c.output.find("q7") == std::string::npos);
            c.send("exit\n");
        }
        mnos::checked({"/usr/sbin/userdel", name});
        std::cout << "PASS: long-name console login, wrong-password rejection, hidden input and ordinary-user shell\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (getpwnam(name.c_str())) mnos::run({"/usr/sbin/userdel", name});
        return 1;
    }
}
