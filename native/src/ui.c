/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "process.h"
#include "mnfs.h"
#include "auth.h"
struct glyph {
    uint32_t code, offset;
    uint16_t width, height, advance, size;
};
extern const struct glyph font_glyphs[];
extern const unsigned font_count;
extern const unsigned char font_pixels[];
static uint32_t canvas[2560 * 1440] __attribute__((aligned(4096)));
static int width, height, sidebar, page, cursor_x = 640, cursor_y = 360, old_x = 640, old_y = 360;
static bool ready, dirty = true, cursor_dirty, left_down, power_dialog;
static char command[256], history[14][100];
static unsigned command_length, history_count;
static unsigned wallpaper;
static char setting_status[100];
static const unsigned modes[][2] = {{1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
static const char *wallpapers[] = {"流线", "网格", "纯色"};
static const uint32_t BG = 0x191f27, PANEL = 0x222b35, FG = 0xe2ebf0, MUTED = 0x92a4b2,
                      ACCENT = 0x83d5bc;
static uint32_t blend(uint32_t fg, uint32_t bg, unsigned a) {
    unsigned r = (((fg >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255;
    unsigned g = (((fg >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255;
    unsigned b = ((fg & 255) * a + (bg & 255) * (255 - a)) / 255;
    return (r << 16) | (g << 8) | b;
}
static void pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < width && y >= 0 && y < height)
        canvas[y * width + x] = color;
}
static void rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > width ? width : x + w,
        y1 = y + h > height ? height : y + h;
    for (int j = y0; j < y1; ++j)
        for (int i = x0; i < x1; ++i)
            canvas[j * width + i] = color;
}
static void rounded(int x, int y, int w, int h, uint32_t color) {
    rect(x + 6, y, w - 12, h, color);
    rect(x, y + 6, w, h - 12, color);
    for (int j = 0; j < 6; ++j)
        for (int i = 0; i < 6; ++i)
            if ((i - 5) * (i - 5) + (j - 5) * (j - 5) <= 25) {
                pixel(x + i, y + j, color);
                pixel(x + w - 1 - i, y + j, color);
                pixel(x + i, y + h - 1 - j, color);
                pixel(x + w - 1 - i, y + h - 1 - j, color);
            }
}
static uint32_t utf8(const char **text) {
    const uint8_t *s = (const uint8_t *)*text;
    uint32_t c = *s++;
    unsigned n = 0;
    if (c >= 0xc0 && c < 0xe0) {
        c &= 31;
        n = 1;
    } else if (c >= 0xe0 && c < 0xf0) {
        c &= 15;
        n = 2;
    } else if (c >= 0xf0) {
        c = '?';
    }
    while (n--) {
        if ((*s & 0xc0) != 0x80) {
            c = '?';
            break;
        }
        c = (c << 6) | (*s++ & 63);
    }
    *text = (const char *)s;
    return c;
}
static void text(int x, int y, const char *s, unsigned size, uint32_t color) {
    while (*s && x < width - 8) {
        uint32_t c = utf8(&s);
        const struct glyph *g = NULL;
        for (unsigned i = 0; i < font_count; ++i)
            if (font_glyphs[i].code == c && font_glyphs[i].size == size) {
                g = &font_glyphs[i];
                break;
            }
        if (!g) {
            x += size / 2;
            continue;
        }
        for (unsigned j = 0; j < g->height; ++j)
            for (unsigned i = 0; i < g->width; ++i) {
                unsigned a = font_pixels[g->offset + j * g->width + i];
                int px = x + (int)i, py = y + (int)j;
                if (a && px >= 0 && py >= 0 && px < width && py < height)
                    pixel(px, py, blend(color, canvas[py * width + px], a));
            }
        x += g->advance;
    }
}
static void numeric(int x, int y, uint64_t n, unsigned size, uint32_t color) {
    char b[24];
    number(b, n);
    text(x, y, b, size, color);
}
static void display_rect(int x, int y, int w, int h) {
    for (int j = y < 0 ? 0 : y; j < y + h && j < height; ++j)
        for (int i = x < 0 ? 0 : x; i < x + w && i < width; ++i) {
            uint32_t c = canvas[j * width + i];
            boot.fb.pixels[j * (boot.fb.pitch / 4) + i] = (((c >> 16) & 255) << boot.fb.red) |
                                                          (((c >> 8) & 255) << boot.fb.green) |
                                                          ((c & 255) << boot.fb.blue);
        }
}
static void cursor(void) {
    for (int j = 0; j < 19; ++j)
        for (int i = 0; i <= j / 2; ++i) {
            int x = cursor_x + i, y = cursor_y + j;
            if (x < 0 || y < 0 || x >= width || y >= height)
                continue;
            uint32_t c = i == 0 || i == j / 2 || j == 18 ? 0x14202b : 0xe9f3f5;
            boot.fb.pixels[y * (boot.fb.pitch / 4) + x] = (((c >> 16) & 255) << boot.fb.red) |
                                                          (((c >> 8) & 255) << boot.fb.green) |
                                                          ((c & 255) << boot.fb.blue);
        }
    old_x = cursor_x;
    old_y = cursor_y;
    cursor_dirty = false;
}
static void card(int x, int y, int w, const char *title, uint64_t value, const char *unit) {
    rounded(x, y, w, 116, PANEL);
    text(x + 18, y + 12, title, 18, MUTED);
    numeric(x + 18, y + 43, value, 28, FG);
    text(x + 18, y + 84, unit, 18, MUTED);
}
static void line(int x, int y, int w, const char *label, const char *value) {
    text(x, y, label, 18, MUTED);
    text(x + w / 2, y, value, 18, FG);
    rect(x, y + 34, w, 1, 0x2c3641);
}
static void add_line(const char *s) {
    log_write("OUT: "); log_write(s); log_write("\n");
    if (history_count == ARRAY_SIZE(history)) {
        for (unsigned i = 1; i < history_count; ++i)
            memcpy(history[i - 1], history[i], sizeof history[i]);
        --history_count;
    }
    unsigned i = 0;
    while (s[i] && i < sizeof history[0] - 1) {
        history[history_count][i] = s[i];
        ++i;
    }
    history[history_count++][i] = 0;
}
static void add_number(const char *label, uint64_t n) {
    char row[100], num[24];
    number(num, n);
    unsigned i = 0;
    while (*label && i < 70)
        row[i++] = *label++;
    for (unsigned j = 0; num[j] && i < 99; ++j)
        row[i++] = num[j];
    row[i] = 0;
    add_line(row);
}
void ui_program_output(const char *s) { add_line(s); dirty = true; }
static void setting_message(const char *s) {
    size_t n = strlen(s); if (n >= sizeof setting_status) n = sizeof setting_status - 1;
    memcpy(setting_status, s, n); setting_status[n] = 0; dirty = true;
}
static void resolution_text(char *out) {
    number(out, boot.fb.width); size_t n = strlen(out); out[n++] = 'x';
    number(out + n, boot.fb.height);
}
static bool set_resolution(unsigned w, unsigned h) {
    if (!graphics_mode(w, h)) {
        setting_message("当前显卡不支持此模式；可在 Linux 中调整显示。");
        return false;
    }
    ui_init(); setting_message("分辨率已更新。保存后可在下次启动恢复。");
    return true;
}
static void change_resolution(void) {
    unsigned next = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(modes); ++i)
        if (boot.fb.width == modes[i][0] && boot.fb.height == modes[i][1]) next = (i + 1) % ARRAY_SIZE(modes);
    for (unsigned attempt = 0; attempt < ARRAY_SIZE(modes); ++attempt) {
        unsigned index = (next + attempt) % ARRAY_SIZE(modes);
        if (set_resolution(modes[index][0], modes[index][1])) return;
    }
}
static void change_wallpaper(unsigned n) {
    wallpaper = n;
    setting_message("壁纸已更新。保存后可在下次启动恢复。");
    log_write("SETTINGS: wallpaper="); log_dec(wallpaper); log_write("\n");
}
static void save_settings(void) {
    unsigned w = boot.fb.width, h = boot.fb.height;
    int e = files_settings(&w, &h, &wallpaper, true);
    setting_message(e == MN_OK ? "显示设置已保存到个人目录。" :
                    e == MN_RO ? "磁盘为只读。需要 root 解锁后再保存。" :
                    e == MN_NOENT ? "未挂载 MNFS 磁盘；当前设置在重启后重置。" : "保存失败；请检查磁盘状态。");
    log_write("SETTINGS: "); log_write(e ? mnfs_error(e) : "saved"); log_write("\n");
}
void ui_load_settings(void) {
    unsigned w, h, background;
    if (files_settings(&w, &h, &background, false) != MN_OK) return;
    wallpaper = background;
    if (w == boot.fb.width && h == boot.fb.height) dirty = true;
    else if (graphics_mode(w, h)) ui_init();
    setting_message("已读取 MNFS 磁盘上的显示设置。");
    log_write("SETTINGS: restored wallpaper="); log_dec(wallpaper); log_write("\n");
}
static void help(bool chinese) {
    if (chinese) {
        add_line("mnOS 中文帮助：help -cn；英文帮助：help");
        add_line("普通用户：查询、个人目录读写、显示设置；管理卷需要 root。");
        add_line("whoami / passwd / su / exit / logout；root 可用 useradd 用户名。");
        add_line("info / memory / cpu / gpu / devices / test：查看状态");
        add_line("kernel display 1600x900：调整受支持的显示模式");
        add_line("kernel wallpaper 0|1|2：流线、网格、纯色");
        add_line("kernel save-settings：保存显示设置到 MNFS 磁盘");
        add_line("fs / ls / stat / cat：文件系统信息、列表及读取");
        add_line("exec /ram/bin/hello；ps；kill PID");
        add_line("write 路径 内容；writeat 路径 偏移 内容");
        add_line("mkdir / rm / mv / truncate：创建、删除、移动、截断");
        add_line("disk unlock|lock：root 开启或关闭 /disk 写入");
        add_line("snapshot save|restore|drop /disk：保存、恢复、删除快照");
        add_line("/ram 临时；/disk 持久。F7 打开显示设置。");
    } else {
        add_line("mnOS command guide (English); help -cn for Chinese.");
        add_line("Users: queries, own-home files and display settings. Volume administration: root.");
        add_line("whoami / passwd / su / exit / logout; root: useradd NAME, passwd NAME");
        add_line("kernel info|memory|cpu|gpu|devices|test|help");
        add_line("kernel display WIDTHxHEIGHT / wallpaper 0|1|2 / save-settings");
        add_line("help  uname  mem  tasks  pci  test  uptime  clear  reboot  shutdown");
        add_line("fs  ls [PATH]  stat PATH  cat PATH [OFFSET]");
        add_line("exec PATH: run a native ELF; ps: process; kill PID: stop your process");
        add_line("write PATH TEXT / writeat PATH OFFSET TEXT / truncate PATH SIZE");
        add_line("mkdir PATH / rm PATH / mv FROM TO / gc [/ram|/disk]");
        add_line("disk unlock|lock; snapshot save|restore|drop [/ram|/disk]");
        add_line("/ram is temporary; /disk is persistent. Sign in to write your own files.");
        add_line("F1-F7: pages; F7: display settings; kernel help -cn: Chinese.");
    }
}
static void kernel_command(const char *cmd) {
    if (!*cmd || streq(cmd, "help")) help(false);
    else if (streq(cmd, "help -cn")) help(true);
    else if (streq(cmd, "info")) {
        add_line("mnOS native / " MNOS_ARCH " / original C + assembly kernel");
        add_line(cpu_brand); add_number("Online CPUs: ", cpu_online_count());
    } else if (streq(cmd, "cpu")) {
        add_line(cpu_brand); add_number("Discovered CPUs: ", cpu_count());
        add_number("Online CPUs: ", cpu_online_count());
    } else if (streq(cmd, "memory")) {
        add_number("Free physical pages (4 KiB): ", pages_free());
        add_number("Usable physical pages (4 KiB): ", pages_total());
        add_number("Physical address limit (bytes): ", memory_limit());
    } else if (streq(cmd, "gpu")) add_line(graphics_driver());
    else if (streq(cmd, "devices")) {
        for (unsigned i = 0; i < pci_count; ++i) {
            char row[100]; pci_read_line(i, row, sizeof row); add_line(row);
        }
    } else if (streq(cmd, "test")) {
        add_number("Boot CPU isolation checks: ", test_passed);
        add_line("Per-CPU results are in the serial log.");
    } else if (streq(cmd, "display")) {
        char mode[32]; resolution_text(mode); add_line(mode);
    } else if (!strncmp(cmd, "display ", 8)) {
        bool matched = false;
        for (unsigned i = 0; i < ARRAY_SIZE(modes); ++i) {
            char mode[32]; number(mode, modes[i][0]); size_t n = strlen(mode);
            mode[n++] = 'x'; number(mode + n, modes[i][1]);
            if (streq(cmd + 8, mode)) {
                matched = true; add_line(set_resolution(modes[i][0], modes[i][1]) ? "Display mode applied." : "Display mode unavailable.");
                break;
            }
        }
        if (!matched) add_line("Modes: 1024x768 1280x720 1600x900 1920x1080 2560x1440");
    } else if (!strncmp(cmd, "wallpaper ", 10) && cmd[10] >= '0' && cmd[10] <= '2' && !cmd[11]) {
        change_wallpaper(cmd[10] - '0'); add_line("Wallpaper applied.");
    } else if (streq(cmd, "save-settings")) { save_settings(); add_line(setting_status); }
    else if (!files_command(cmd, add_line)) add_line("Unknown instruction or invalid object. Type kernel help.");
}
static void execute(void) {
    command[command_length] = 0;
    add_line(command);
    if(auth_command(command,add_line)) {}
    else if (streq(command, "help")) help(false);
    else if (streq(command, "help -cn")) help(true);
    else if (streq(command, "kernel")) kernel_command("");
    else if (!strncmp(command, "kernel ", 7)) kernel_command(command + 7);
    else if (streq(command, "uname"))
        add_line("mnOS native 0.1 / " MNOS_ARCH " / freestanding C + assembly");
    else if (streq(command, "mem")) {
        add_number("Free physical pages: ", pages_free());
        add_number("Usable physical pages: ", pages_total());
    } else if (streq(command, "uptime"))
        add_number("Uptime (seconds): ", ticks / PIT_HZ);
    else if (streq(command, "tasks")) {
        add_number("Task 0 switches: ", tasks[0].switches);
        add_number("Task 1 switches: ", tasks[1].switches);
    } else if (streq(command, "pci")) {
        for (unsigned i = 0; i < pci_count && i < 10; ++i) {
            char row[100];
            pci_read_line(i, row, sizeof row);
            add_line(row);
        }
    } else if (streq(command, "test")) {
        add_number("Boot isolation checks passed: ", test_passed);
        add_line("Expected: 8. See serial log for individual checks.");
    } else if (streq(command, "clear"))
        history_count = 0;
    else if (streq(command, "reboot"))
        reboot();
    else if (streq(command, "shutdown"))
        power_dialog = true;
    else if (files_command(command, add_line)) {}
    else if (command_length)
        add_line("Unknown command. Type help.");
    log_write("CONSOLE: ");
    log_write(command);
    log_write("\n");
    command_length = 0;
    command[0] = 0;
}
static void draw(void) {
    rect(0, 0, width, height, BG);
    if(auth_prompting()) {
        int x=width/2-390,y=height/2-175;
        rounded(x,y,780,350,PANEL);
        text(x+28,y+22,"mnOS / Account authentication",28,FG);
        text(x+28,y+82,auth_status(),18,MUTED);
        text(x+28,y+136,auth_prompt(),18,ACCENT);
        rounded(x+28,y+185,724,52,0x10171e);
        text(x+42,y+197,auth_input(),18,FG);
        text(x+28,y+264,"Enter: continue    Esc: cancel or return to sign in",18,MUTED);
        display_rect(0,0,width,height);dirty=false;return;
    }
    if (wallpaper == 0) {
        for (int n = 0; n < 18; ++n)
            for (int x = sidebar; x < width; ++x) {
                int d = (x - width / 2) / 16;
                pixel(x, height - 75 - n * 15 + d * d / 16, 0x263c3c);
            }
    } else if (wallpaper == 1) {
        for (int x = sidebar; x < width; x += 40) rect(x, 0, 1, height, 0x25313b);
        for (int y = 0; y < height; y += 40) rect(sidebar, y, width - sidebar, 1, 0x25313b);
    }
    rect(0, 0, sidebar, height, 0x131920);
    text(25, 25, "mnOS", 28, FG);
    text(26, 66, "原生内核 · 0.1", 18, MUTED);
    const char *names[] = {"概览", "内存与进程", "设备", "安全自检", "内核终端", "工具环境", "显示与设置"};
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
        int y = 130 + (int)i * 55;
        if ((int)i == page)
            rounded(14, y, sidebar - 28, 44, 0x28463f);
        text(29, y + 8, names[i], 18, (int)i == page ? ACCENT : MUTED);
    }
    text(25, height - 71, "F1—F7 切换页面", 18, MUTED);
    text(25, height - 43, MNOS_ARCH " / 实验版", 18, MUTED);
    int x = sidebar + 30, w = width - sidebar - 60;
    text(x, 28, names[page], 28, FG);
    text(x, 71, "mnOS 自研内核", 18, MUTED);
    text(width - 196, 37, "运行时间", 18, MUTED);
    numeric(width - 96, 37, ticks / PIT_HZ, 18, ACCENT);
    text(width - 38, 37, "s", 18, MUTED);
    rect(x, 110, w, 1, 0x2e3943);
    if (page == 0) {
        int cw = (w - 24) / 3;
        card(x, 135, cw, "在线处理器", cpu_online_count(), "每核独立调度");
        uint64_t bytes = pages_free() * PAGE;
        uint64_t unit = bytes >= (1ULL << 40) ? (1ULL << 40) : bytes >= (1ULL << 30) ? (1ULL << 30) : (1ULL << 20);
        card(x + cw + 12, 135, cw, "可用内存", bytes / unit, unit == (1ULL << 40) ? "TiB" : unit == (1ULL << 30) ? "GiB" : "MiB");
        card(x + 2 * cw + 24, 135, cw, "启动自检", test_passed, "项已通过 / 共 8 项");
        text(x, 282, "内核状态", 18, FG);
        line(x, 324, w, "处理器", cpu_brand);
        line(x, 368, w, "调度", "多核抢占式 / 每核 100 Hz");
        line(x, 412, w, "内存保护", "用户态隔离 · W^X · NX");
#ifdef __aarch64__
        line(x, 456, w, "显示与输入", "GOP / VirtIO / PL011");
#else
        line(x, 456, w, "显示与输入", "帧缓冲 · PS/2 · 串口");
#endif
        rounded(x, 527, w, 78, 0x23332f);
        text(x + 18, 541, "这是原生内核的早期版本。", 18, ACCENT);
        text(x + 18, 570, "浏览器、CTF 与 Wi-Fi 在 Linux 工具环境中运行。", 18, MUTED);
    } else if (page == 1) {
        uint64_t bytes = pages_total() * PAGE, unit = bytes >= (1ULL << 40) ? (1ULL << 40) : (1ULL << 20);
        card(x, 135, (w - 12) / 2, "已管理物理内存", bytes / unit, unit == (1ULL << 40) ? "TiB" : "MiB");
        card(x + (w + 12) / 2, 135, (w - 12) / 2, "可分配物理页", pages_free(), "4 KiB / page");
        text(x, 286, "进程", 18, FG);
        text(x + w / 3, 286, "切换次数", 18, MUTED);
        text(x + 2 * w / 3, 286, "用户态计数", 18, MUTED);
        for (unsigned i = 0; i < 2; ++i) {
            int y = 334 + (int)i * 64;
            text(x, y, i ? "01  worker-b" : "00  worker-a", 18, FG);
            numeric(x + w / 3, y, tasks[i].switches, 18, ACCENT);
            numeric(x + 2 * w / 3, y, tasks[i].data ? *(uint64_t *)phys_pointer(tasks[i].data) : 0, 18, FG);
            rect(x, y + 42, w, 1, 0x2c3641);
        }
        text(x, 498, "此处显示启动核的两个进程；其他核拥有独立进程和页表。", 18, MUTED);
        text(x, 536, "内核数据不可从用户态读取，代码页不可写入。", 18, MUTED);
    } else if (page == 2) {
#ifdef __aarch64__
        text(x, 139, "VirtIO MMIO", 18, FG);
#else
        text(x, 139, "PCI 设备枚举", 18, FG);
#endif
        numeric(x + w - 45, 139, pci_count, 18, ACCENT);
        for (unsigned i = 0; i < pci_count && i < 9; ++i) {
            char row[100];
            pci_read_line(i, row, sizeof row);
            text(x, 184 + (int)i * 36, row, 18, MUTED);
        }
        text(x, 548, "设备被识别不代表驱动已实现。", 18, ACCENT);
        text(x, 582, "通用网卡、Wi-Fi 和 USB 驱动由 Linux 工具环境提供。", 18, MUTED);
    } else if (page == 3) {
        const char *checks[] = {
            "物理页分配与内核页权限",     "用户态访问内核内存被阻止", "数据页禁止执行",
            "用户栈保护页生效",           "系统调用检查用户指针",     "用户代码页禁止写入",
            "用户态禁止直接访问硬件端口", "时钟抢占与独立地址空间"};
        for (unsigned i = 0; i < SELFTEST_COUNT; ++i) {
            int y = 137 + (int)i * 46;
            rounded(x, y, w, 39, PANEL);
            text(x + 16, y + 5, checks[i], 18, FG);
            text(x + w - 70, y + 5, test_passed == SELFTEST_COUNT ? "通过" : "自检中", 18, ACCENT);
        }
#ifdef __aarch64__
        text(x, 521, "EL0 / EL1 · W^X · PXN / UXN", 18, MUTED);
#else
        text(x, 521, cpu_smep ? "SMEP 已启用" : "CPU 未提供 SMEP", 18, MUTED);
        text(x + w / 2, 521, cpu_smap ? "SMAP 已启用" : "CPU 未提供 SMAP", 18, MUTED);
#endif
        text(x, 570, "当前结果仅覆盖启动自检，仍需持续测试与安全审计。", 18, MUTED);
    } else if (page == 4) {
        rounded(x, 135, w, height - 175, 0x10171e);
        text(x + 18, 149, "mnOS kernel console / help 查看命令", 18, ACCENT);
        text(x+w-180,149,auth_name(),18,MUTED);
        unsigned max = (height - 265) / 28, start = history_count > max ? history_count - max : 0;
        for (unsigned i = start; i < history_count; ++i)
            text(x + 18, 190 + (int)(i - start) * 28, history[i], 18, FG);
        int cy = height - 81;
        text(x + 18, cy, ">", 18, ACCENT);
        text(x + 38, cy, command, 18, FG);
        text(x + 38 + (int)command_length * 10, cy, "_", 18, ACCENT);
    } else if (page == 5) {
        text(x, 146, "Linux 工具环境", 28, FG);
        text(x, 198, "在启动菜单中选择另一个系统入口。", 18, MUTED);
        line(x, 254, w, "浏览器", "Firefox ESR");
        line(x, 302, w, "CTF", "GDB · Pwntools · Nmap");
        line(x, 350, w, "网络与取证", "Wireshark · Hashcat · Binwalk");
        line(x, 398, w, "硬件支持", "常见显卡、网卡与 Wi-Fi 固件");
        rounded(x, 476, 280, 52, 0x31584b);
        text(x + 19, 488, "重启并选择工具环境", 18, ACCENT);
        text(x, 562, "原生内核与 Linux 工具环境是两个独立的运行环境。", 18, MUTED);
    } else {
        line(x, 140, w, "显示驱动", graphics_driver());
        char mode[32]; resolution_text(mode);
        rounded(x, 207, w, 70, PANEL);
        text(x + 18, 218, "分辨率", 18, MUTED); text(x + 180, 218, mode, 28, FG);
        text(x + 18, 250, "点击切换 / R", 18, ACCENT);
        rounded(x, 293, w, 70, PANEL);
        text(x + 18, 304, "壁纸", 18, MUTED); text(x + 180, 304, wallpapers[wallpaper], 28, FG);
        text(x + 18, 336, "点击切换 / W", 18, ACCENT);
        rounded(x, 386, 280, 52, 0x31584b);
        text(x + 18, 398, "保存到磁盘 / S", 18, ACCENT);
        text(x, 465, setting_status, 18, FG);
        text(x, 514, "持久保存需要 MNFS 磁盘；登录后可保存个人设置。", 18, MUTED);
        text(x, 552, "Linux 桌面另提供网络、声音、输入、账号和多屏等完整设置。", 18, MUTED);
    }
    if (power_dialog) {
        int dx = (width - 500) / 2, dy = (height - 220) / 2;
        rounded(dx, dy, 500, 220, 0x33424d);
        text(dx + 28, dy + 25, "电源", 28, FG);
        text(dx + 28, dy + 83, "R 重启  /  S 关机  /  Esc 取消", 18, FG);
        text(dx + 28, dy + 132, "重启后可在菜单选择 Linux 工具环境。", 18, MUTED);
    }
    display_rect(0, 0, width, height);
    cursor();
    dirty = false;
}
void ui_init(void) {
    if (!boot.fb.pixels)
        return;
    width = (int)boot.fb.width;
    height = (int)boot.fb.height;
    sidebar = width < 1000 ? 190 : 226;
    ready = true;
    cursor_x = width / 2;
    cursor_y = height / 2;
    add_line("Native kernel console. Type help.");
    draw();
}
void ui_tick(void) {
    if (!ready)
        return;
    auth_tick(ticks/PIT_HZ);
    process_session_check();
    static uint32_t last_user=UINT32_MAX;
    if(auth_uid()!=last_user) {
        last_user=auth_uid();history_count=0;auth_wipe(command,sizeof command);command_length=0;
        wallpaper=0;power_dialog=false;dirty=true;
        if(auth_logged_in()) {files_session();ui_load_settings();}
    }
    static const char *last_prompt,*last_status;
    if(last_prompt!=auth_prompt()||last_status!=auth_status()) {
        last_prompt=auth_prompt();last_status=auth_status();dirty=true;
        log_write("AUTH: ");log_write(last_status);log_write("\nAUTH PROMPT: ");log_write(last_prompt);log_write("\n");
    }
    if (ticks % PIT_HZ == 0)
        dirty = true;
    if (dirty && ticks % 5 == 0)
        draw();
    else if (cursor_dirty && ticks % 3 == 0) {
        display_rect(old_x, old_y, 12, 21);
        cursor();
    }
}
static void select_page(int n) {
    page = n;
    dirty = true;
    log_write("UI: page ");
    log_dec(n);
    log_write("\n");
}
void ui_key(char key) {
    if(auth_prompting()) {auth_key(key);dirty=true;return;}
    unsigned char k = (unsigned char)key;
    if (power_dialog) {
        if (key == 'r' || key == 'R')
            reboot();
        else if (key == 's' || key == 'S')
            power_off();
        else if (key == 27)
            power_dialog = false;
        dirty = true;
        return;
    }
    if (k >= 0x81 && k <= 0x87) {
        select_page(k - 0x81);
        return;
    }
    if (key == 27) {
        select_page(0);
        return;
    }
    if (page != 4) {
        if (page == 6 && key == 'r') change_resolution();
        else if (page == 6 && key == 'w') change_wallpaper((wallpaper + 1) % ARRAY_SIZE(wallpapers));
        else if (page == 6 && key == 's') save_settings();
        else if (key >= '1' && key <= '7')
            select_page(key - '1');
        else if (key == 't')
            select_page(4);
        else if (key == 'p') {
            power_dialog = true;
            dirty = true;
        }
        return;
    }
    if (key == '\n' || key == '\r')
        execute();
    else if (key == '\b' || key == 127) {
        if (command_length)
            command[--command_length] = 0;
    } else if (key >= 32 && key < 127 && command_length < sizeof command - 1) {
        command[command_length++] = key;
        command[command_length] = 0;
    }
    dirty = true;
}
void ui_pointer(int x, int y, bool pressed) {
    if(auth_prompting()) {left_down=pressed;return;}
    cursor_x = x;
    cursor_y = y;
    cursor_dirty = true;
    if (pressed && !left_down && !power_dialog) {
        if (x < sidebar && y >= 130 && y < 515) {
            int n = (y - 130) / 55;
            if (n < 7)
                select_page(n);
        } else if (page == 5 && x >= sidebar + 30 && x < sidebar + 310 && y >= 476 && y < 528) {
            power_dialog = true;
            dirty = true;
        } else if (page == 6 && x >= sidebar + 30 && x < width - 30) {
            if (y >= 207 && y < 277) change_resolution();
            else if (y >= 293 && y < 363) change_wallpaper((wallpaper + 1) % ARRAY_SIZE(wallpapers));
            else if (x < sidebar + 310 && y >= 386 && y < 438) save_settings();
        }
    }
    left_down = pressed;
}
void ui_panic(const char *message) {
    if (!ready)
        return;
    rect(0, 0, width, height, 0x251e24);
    text(40, 40, "mnOS / kernel stopped", 28, 0xf4bdba);
    text(40, 110, message, 18, FG);
    text(40, 165, "See the serial console for fault details.", 18, MUTED);
    display_rect(0, 0, width, height);
}
