/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unprivileged Linux kernel queries and an explicit administration dispatcher.
 */
#define _GNU_SOURCE
#include "kernel-common.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
static void help(void) {
    puts("kernel <instruction> [object]\n"
         "  info | memory | cpu | modules | devices | gpu | irq | log\n"
         "  sysctl KEY              Read a kernel parameter\n"
         "Read-only queries run as the current user. Restricted logs/parameters\n"
         "remain subject to Linux permissions; use sudo only when authorized.\n\n"
         "Administration (sudo kernel <instruction> [object]):\n"
         "  load MODULE             Load a kernel module\n"
         "  unload MODULE           Unload a module if it is not in use\n"
         "  set KEY=VALUE           Change a runtime kernel parameter\n"
         "  cpu-online NUMBER       Online a hotpluggable CPU\n"
         "  cpu-offline NUMBER      Offline a non-boot CPU\n"
         "  performance PROFILE     balanced | throughput | virtual\n"
         "Only administrative operations require root. No automatic elevation.\n"
         "Runtime sysctl/CPU changes reset on reboot; TuneD profile selection persists.\n"
         "GPU output reports the bound driver; it does not certify acceleration.");
    puts("English: kernel help    Chinese: kernel help -cn");
}
static void help_cn(void) {
    puts("kernel <指令> [对象]\n"
         "  info                    内核版本和在线 CPU\n"
         "  memory | cpu | irq      内存、处理器、中断信息\n"
         "  modules | devices | gpu 已加载模块、PCI 设备、显卡驱动\n"
         "  log                     内核日志\n"
         "  sysctl 参数名           读取内核参数\n"
         "查询以当前用户运行；受限制的日志和参数仍遵循 Linux 权限。\n\n"
         "管理操作（sudo kernel <指令> [对象]）：\n"
         "  load 模块名             加载内核模块\n"
         "  unload 模块名           卸载未被使用的模块\n"
         "  set 参数名=值           修改运行时内核参数\n"
         "  cpu-online 编号         上线支持热插拔的 CPU\n"
         "  cpu-offline 编号        下线非启动 CPU\n"
         "  performance 配置        balanced / throughput / virtual\n"
         "仅管理操作需要 root，程序不会自动提权。\n"
         "运行时参数和 CPU 状态重启后恢复；TuneD 配置选择会保留。\n"
         "显卡命令显示绑定驱动，不代表已验证加速。\n"
         "英文帮助：kernel help    中文帮助：kernel help -cn");
}
static int show(const char *path) {
    FILE *file = fopen(path, "re");
    if (!file) { perror(path); return 1; }
    char buffer[8192]; size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, file)))
        if (fwrite(buffer, 1, n, stdout) != n) { fclose(file); return 1; }
    int error = ferror(file); fclose(file); return error ? 1 : 0;
}
static int gpu(void) {
    DIR *directory = opendir("/sys/class/drm");
    if (!directory) {
        if (errno == ENOENT) { puts("No DRM display device is currently registered."); return 0; }
        perror("DRM"); return 1;
    }
    struct dirent *entry; unsigned count = 0;
    while ((entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (strncmp(name, "card", 4) || !isdigit((unsigned char)name[4])) continue;
        const char *p = name + 4; while (isdigit((unsigned char)*p)) ++p;
        if (*p) continue;
        char path[512], driver[512]; snprintf(path, sizeof path, "/sys/class/drm/%s/device/driver", name);
        ssize_t n = readlink(path, driver, sizeof driver - 1);
        if (n >= 0) { driver[n] = 0; const char *base = strrchr(driver, '/'); printf("%s driver=%s\n", name, base ? base + 1 : driver); }
        else printf("%s driver=unbound or firmware framebuffer\n", name);
        ++count;
    }
    closedir(directory);
    if (!count) puts("No DRM display device is currently registered.");
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "help") && !strcmp(argv[2], "-cn")) { help_cn(); return 0; }
    if (argc == 1 || (argc == 2 && (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help")))) { help(); return 0; }
    if (argc > 3) { help(); return 2; }
    const char *cmd = argv[1]; char *object = argc == 3 ? argv[2] : NULL;
    if (!strcmp(cmd, "info") && !object) {
        struct utsname name; if (uname(&name)) return 1;
        printf("%s %s %s\n", name.sysname, name.release, name.machine);
        puts("Online CPUs:"); return show("/sys/devices/system/cpu/online");
    }
    if (!strcmp(cmd, "memory") && !object) return show("/proc/meminfo");
    if (!strcmp(cmd, "cpu") && !object) return show("/proc/cpuinfo");
    if (!strcmp(cmd, "irq") && !object) return show("/proc/interrupts");
    if (!strcmp(cmd, "gpu") && !object) return gpu();
    if (!strcmp(cmd, "modules") && !object) return execute((char *[]){"/usr/sbin/lsmod", NULL});
    if (!strcmp(cmd, "devices") && !object) return execute((char *[]){"/usr/bin/lspci", "-nnk", NULL});
    if (!strcmp(cmd, "log") && !object) return execute((char *[]){"/usr/bin/dmesg", "--color=never", NULL});
    if (!strcmp(cmd, "sysctl") && identifier(object, true))
        return execute((char *[]){"/usr/sbin/sysctl", "--", object, NULL});
    if (!strcmp(cmd, "load") || !strcmp(cmd, "unload") || !strcmp(cmd, "set") ||
        !strcmp(cmd, "cpu-online") || !strcmp(cmd, "cpu-offline") || !strcmp(cmd, "performance"))
        return execute((char *[]){"/usr/sbin/mnos-kernel-admin", argv[1], object, NULL});
    fputs("Unknown instruction or invalid object. Run kernel help.\n", stderr); return 2;
}
