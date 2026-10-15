// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"
#include <cstdio>
#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
static constexpr const char *helper = "/usr/lib/mnos/net-admin";
static void policy(const char *value) {
    if (mkdir("/etc/mnos", 0755) && errno != EEXIST)
        throw std::runtime_error("Cannot create policy directory");
    int fd = open("/etc/mnos/network-policy.new",
                  O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0)
        throw std::runtime_error("Cannot write policy");
    bool ok = fchmod(fd, 0644) == 0 && write(fd, value, strlen(value)) == ssize_t(strlen(value)) &&
              fsync(fd) == 0;
    close(fd);
    if (!ok || rename("/etc/mnos/network-policy.new", "/etc/mnos/network-policy"))
        throw std::runtime_error("Cannot commit policy");
}
int main(int argc, char **argv) {
    try {
        if (getuid() != 0 || geteuid() != 0)
            throw std::runtime_error("Only root can change network delegation.");
        if (argc < 2 ||
            (std::string(argv[1]) == "root" ? argc != 2
                                            : std::string(argv[1]) != "netadmin" || argc != 3))
            throw std::runtime_error("Usage: mnos-network-policy root | netadmin USER");
        bool enable = std::string(argv[1]) == "netadmin";
        if (enable) {
            if (!mnos::username(argv[2]))
                throw std::runtime_error("Invalid username");
            auto *u = getpwnam(argv[2]);
            if (!u || u->pw_uid < 1000 || u->pw_uid == 65534)
                throw std::runtime_error("Choose an ordinary local account");
        }
        auto *group = getgrnam("mnos-netadmin");
        if (!group)
            throw std::runtime_error("Network group missing");
        int fd = open(helper, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        struct stat st{};
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0)
            throw std::runtime_error("Helper must be a regular root-owned file");
        if (fremovexattr(fd, "security.capability") && errno != ENODATA)
            throw std::runtime_error("Cannot revoke capabilities");
        if (fchown(fd, 0, group->gr_gid) || fchmod(fd, 0750))
            throw std::runtime_error("Cannot secure helper");
        close(fd);
        policy("root\n");
        if (enable) {
            mnos::checked({"/usr/sbin/usermod", "-a", "-G", "mnos-netadmin", argv[2]});
            try {
                mnos::checked({"/usr/sbin/setcap", "cap_net_admin,cap_net_raw=ep", helper});
                mnos::checked({"/usr/sbin/setcap", "-v", "cap_net_admin,cap_net_raw=ep", helper});
                policy("netadmin\n");
            } catch (...) {
                removexattr(helper, "security.capability");
                throw;
            }
            std::cout << "Network delegation enabled. Sign out and back in to refresh groups.\n";
        } else {
            mnos::checked({"/usr/bin/gpasswd", "-M", "", "mnos-netadmin"});
            std::cout << "Network delegation disabled. End existing tool sessions to revoke "
                         "inherited capabilities.\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
