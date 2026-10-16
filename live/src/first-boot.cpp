// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <termios.h>
static volatile sig_atomic_t interrupted = 0;
static void interrupt(int) {
    interrupted = 1;
}
static std::string username_line() {
    // Use the same unbuffered descriptor as password input. A stdio buffer
    // must not retain input while the terminal switches canonical modes.
    std::string result;
    while (!interrupted) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n != 1)
            break;
        if (c == '\n' || c == '\r')
            return result;
        result += c;
    }
    throw std::runtime_error("Setup interrupted");
}
static std::string password(const char *prompt) {
    termios old{};
    if (tcgetattr(STDIN_FILENO, &old))
        throw std::runtime_error("A local terminal is required");
    termios hidden = old;
    hidden.c_lflag &= ~(ECHO | ICANON);
    hidden.c_cc[VMIN] = 1;
    hidden.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden))
        throw std::runtime_error("Cannot hide password input");
    std::cout << prompt << std::flush;
    std::string result;
    while (!interrupted) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n != 1) {
            interrupted = 1;
            break;
        }
        if (c == '\n' || c == '\r')
            break;
        if (c == 4) {
            interrupted = 1;
            break;
        }
        if (c == 127 || c == '\b') {
            if (!result.empty())
                result.pop_back();
            continue;
        }
        if (c == 0)
            continue;
        result += c;
    }
    int restored = tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    std::cout << '\n';
    if (interrupted || restored) {
        mnos::erase(result);
        throw std::runtime_error("Setup interrupted; no session unlocked");
    }
    return result;
}
int main() {
    try {
        if (getuid() != 0 || geteuid() != 0)
            throw std::runtime_error("Local enrollment must run as root.");
        const rlimit no_core{0, 0};
        if (setrlimit(RLIMIT_CORE, &no_core))
            throw std::runtime_error("Cannot disable credential core dumps.");
        if (access("/var/lib/mnos/setup-complete", F_OK) == 0)
            return 0;
        struct sigaction action{};
        action.sa_handler = interrupt;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        signal(SIGPIPE, SIG_IGN);
        int console = open("/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (console >= 0) {
            ioctl(console, 0x5606, 1);
            close(console);
        }
        std::cout << "\033[2J\033[HmnOS / Desktop\n\nCreate your local administrator "
                     "account.\nThis live session runs in RAM. Install to keep changes.\nNo "
                     "default password. Root login stays locked.\n\n";
        while (!interrupted) {
            std::cout << "Username [mnos]: " << std::flush;
            std::string name = username_line();
            if (name.empty())
                name = "mnos";
            if (getpwnam(name.c_str())) {
                std::cout << "此用户名已被系统占用，请填写一个其他的\n";
                continue;
            }
            if (!mnos::username(name)) {
                std::cout << "Use lower-case letters, digits, underscores or hyphens; start with a letter.\n";
                continue;
            }
            if (mnos::run({"/usr/lib/mnos/account-preflight", "--name"}, name + '\n'))
                continue;
            std::string secret = password("Password: ");
            if (secret.empty()) {
                mnos::erase(secret);
                std::cout << "Please enter a password.\n";
                continue;
            }
            std::string input = name + '\n' + secret + '\n';
            int quality = mnos::run({"/usr/lib/mnos/account-preflight", "--check"}, input);
            mnos::erase(input);
            if (quality) {
                mnos::erase(secret);
                continue;
            }
            std::string again = password("Confirm password: ");
            bool same = secret == again;
            mnos::erase(again);
            if (!same) {
                mnos::erase(secret);
                std::cout << "Passwords do not match.\n";
                continue;
            }
            try {
                mnos::checked({"/usr/sbin/useradd", "--create-home", "--shell", "/bin/bash",
                               "--groups", "sudo,netdev,plugdev", name});
            } catch (...) {
                mnos::erase(secret);
                throw;
            }
            input = name + ':' + secret + '\n';
            mnos::erase(secret);
            int result = mnos::run({"/usr/sbin/chpasswd", "--crypt-method", "YESCRYPT"}, input);
            mnos::erase(input);
            if (result) {
                mnos::checked({"/usr/sbin/userdel", "--remove", name});
                std::cout << "Could not set password. Please retry.\n";
                continue;
            }
            if (mkdir("/var/lib/mnos", 0755) && errno != EEXIST)
                throw std::runtime_error("Cannot create setup directory");
            int fd = open("/var/lib/mnos/setup-complete",
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            std::string data = name + '\n';
            if (fd < 0)
                throw std::runtime_error("Cannot save setup marker");
            bool ok = write(fd, data.data(), data.size()) == ssize_t(data.size()) && fsync(fd) == 0;
            close(fd);
            if (!ok) {
                unlink("/var/lib/mnos/setup-complete");
                throw std::runtime_error("Cannot save setup marker");
            }
            std::cout << "Account ready. Starting the graphical login screen...\n";
            return 0;
        }
        throw std::runtime_error("Setup interrupted");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
