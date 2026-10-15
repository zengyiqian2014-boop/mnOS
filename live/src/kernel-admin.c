/* SPDX-License-Identifier: GPL-3.0-or-later
 * Privileged Linux mutations. Installed in sbin, never setuid or file-capable.
 * Check credentials here even when called directly, without the bin frontend.
 */
#define _GNU_SOURCE
#include "kernel-common.h"
#include <errno.h>
#include <fcntl.h>

static int cpu_online(const char *object, bool online) {
    if (!object || !*object || strlen(object) > 10) return 2;
    for (const char *p = object; *p; ++p) if (*p < '0' || *p > '9') return 2;
    errno = 0; char *end; unsigned long id = strtoul(object, &end, 10);
    if (errno || *end || id > 0xffffffffUL || (!online && id == 0)) return 2;
    char path[128]; snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%lu/online", id);
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { perror("CPU hotplug unavailable"); return 1; }
    const char value = online ? '1' : '0';
    ssize_t n = write(fd, &value, 1); int saved = errno; close(fd);
    if (n != 1) { errno = saved; perror("CPU online state"); return 1; }
    printf("CPU %lu %s\n", id, online ? "online" : "offline"); return 0;
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help")))) {
        puts("mnos-kernel-admin <instruction> <object>\n"
             "Administrative backend for sudo kernel <instruction> [object].\n"
             "Instructions: load, unload, set, cpu-online, cpu-offline, performance.\n"
             "Requires real and effective UID 0. Does not elevate privileges.\n"
             "English reference: kernel help    Chinese reference: kernel help -cn");
        return 0;
    }
    if (getuid() != 0 || geteuid() != 0) {
        fputs("This operation changes system state and requires root.\n"
              "Use sudo kernel <instruction> [object] with an authorized account.\n", stderr);
        return 77;
    }
    if (argc != 3) {
        fputs("Expected an instruction and object. Run kernel help.\n", stderr);
        return 2;
    }
    const char *cmd = argv[1]; char *object = argv[2];
    if (!strcmp(cmd, "load") && identifier(object, false))
        return execute((char *[]){"/usr/sbin/modprobe", "--", object, NULL});
    if (!strcmp(cmd, "unload") && identifier(object, false))
        return execute((char *[]){"/usr/sbin/modprobe", "--remove", "--", object, NULL});
    if (!strcmp(cmd, "set") && strlen(object) < 512) {
        char key[512]; memcpy(key, object, strlen(object) + 1);
        char *equals = strchr(key, '=');
        if (!equals || !equals[1]) return 2;
        *equals = 0; if (!identifier(key, true) || strpbrk(equals + 1, "\r\n")) return 2;
        return execute((char *[]){"/usr/sbin/sysctl", "--write", "--", object, NULL});
    }
    if (!strcmp(cmd, "cpu-online")) return cpu_online(object, true);
    if (!strcmp(cmd, "cpu-offline")) return cpu_online(object, false);
    if (!strcmp(cmd, "performance")) {
        char *profile = !strcmp(object, "balanced") ? "mnos-desktop" :
                        !strcmp(object, "throughput") ? "mnos-throughput" :
                        !strcmp(object, "virtual") ? "mnos-virtual" : NULL;
        if (profile) return execute((char *[]){"/usr/sbin/tuned-adm", "profile", profile, NULL});
    }
    fputs("Unknown administrative instruction or invalid object. Run kernel help.\n", stderr); return 2;
}
