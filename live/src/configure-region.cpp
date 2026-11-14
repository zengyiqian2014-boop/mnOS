// SPDX-License-Identifier: GPL-3.0-or-later
#include "region.hpp"
#include "process.hpp"
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
namespace fs = std::filesystem;

static void atomic_file(const fs::path &file, const std::string &data) {
    std::string tmp = file.string() + ".XXXXXX";
    int fd = mkstemp(tmp.data());
    if (fd < 0) throw std::runtime_error("Cannot create settings file");
    size_t at = 0;
    bool ok = fchmod(fd, 0644) == 0;
    while (ok && at < data.size()) {
        ssize_t n = write(fd, data.data() + at, data.size() - at);
        if (n > 0) at += size_t(n);
        else if (n < 0 && errno == EINTR) continue;
        else ok = false;
    }
    if (ok) ok = fsync(fd) == 0;
    if (close(fd)) ok = false;
    if (!ok || rename(tmp.c_str(), file.c_str())) {
        unlink(tmp.c_str()); throw std::runtime_error("Cannot save regional settings");
    }
    int dir = open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) throw std::runtime_error("Cannot sync settings directory");
    int result = fsync(dir); close(dir);
    if (result) throw std::runtime_error("Cannot sync regional settings");
}

int main(int argc, char **argv) {
    try {
        const std::string mode = argc == 2 ? argv[1] : "";
        if (mode != "--check" && mode != "--apply" && mode != "--offline") {
            std::cerr << "Usage: configure-region --check|--apply|--offline < settings\n";
            return 2;
        }
        // A non-setuid utility. The GUI uses pkexec; the installer invokes it in
        // the target chroot. No caller-supplied root path or shell command.
        if (mode != "--check" && geteuid() != 0) throw std::runtime_error("Administrator authentication required");
        std::string input; char c;
        while (std::cin.get(c)) {
            if (input.size() == 8192 || c == '\0') throw std::runtime_error("Invalid regional settings input");
            input += c;
        }
        std::istringstream stream(input);
        auto config = region::parse(stream);
        region::validate(config);
        if (mode == "--check") return 0;
        if (access("/usr/sbin/chronyd", X_OK)) throw std::runtime_error("Install chrony before changing time settings");
        int lock = open("/run/mnos-region.lock", O_CREAT | O_NOFOLLOW | O_RDWR | O_CLOEXEC, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) throw std::runtime_error("Regional settings are being changed by another process");
        fs::create_directories("/etc/mnos");
        fs::create_directories("/etc/chrony");
        // Keep the previous chrony configuration on the first managed change.
        if (fs::exists("/etc/chrony/chrony.conf") && !fs::exists("/etc/chrony/chrony.conf.before-mnos"))
            fs::copy_file("/etc/chrony/chrony.conf", "/etc/chrony/chrony.conf.before-mnos");
        atomic_file("/etc/default/locale", region::locale_file(config));
        atomic_file("/etc/timezone", config.timezone + '\n');
        atomic_file("/etc/chrony/chrony.conf", region::chrony_file(config));
        // Copy TZif rather than trusting or following an existing localtime link.
        std::ifstream zone("/usr/share/zoneinfo/" + config.timezone, std::ios::binary);
        std::ostringstream bytes; bytes << zone.rdbuf();
        if (!zone || bytes.str().size() < 4) throw std::runtime_error("Cannot read time zone");
        atomic_file("/etc/localtime", bytes.str());
        atomic_file("/etc/mnos/region", region::serialize(config));
        if (mode == "--apply") {
            mnos::checked({"/usr/bin/systemctl", "enable", "chrony.service"});
            mnos::checked({"/usr/bin/systemctl", "restart", "chrony.service"});
        }
        close(lock);
        std::cout << "MNOS REGION SAVED\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
