/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
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
static char command[80], history[14][100];
static unsigned command_length, history_count;
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
static void execute(void) {
    command[command_length] = 0;
    add_line(command);
    if (streq(command, "help")) {
        add_line("help  uname  mem  tasks  pci  test  uptime");
        add_line("clear  reboot  shutdown");
    } else if (streq(command, "uname"))
        add_line("mnOS native 0.1 / x86_64 / freestanding C + assembly");
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
    rect(0, 0, sidebar, height, 0x131920);
    text(25, 25, "mnOS", 28, FG);
    text(26, 66, "原生内核 · 0.1", 18, MUTED);
    const char *names[] = {"概览", "内存与进程", "设备", "安全自检", "内核终端", "工具环境"};
    for (unsigned i = 0; i < 6; ++i) {
        int y = 130 + (int)i * 55;
        if ((int)i == page)
            rounded(14, y, sidebar - 28, 44, 0x28463f);
        text(29, y + 8, names[i], 18, (int)i == page ? ACCENT : MUTED);
    }
    text(25, height - 71, "F1—F6 切换页面", 18, MUTED);
    text(25, height - 43, "x86_64 / 实验版", 18, MUTED);
    int x = sidebar + 30, w = width - sidebar - 60;
    text(x, 28, names[page], 28, FG);
    text(x, 71, "mnOS 自研内核", 18, MUTED);
    text(width - 196, 37, "运行时间", 18, MUTED);
    numeric(width - 96, 37, ticks / PIT_HZ, 18, ACCENT);
    text(width - 38, 37, "s", 18, MUTED);
    rect(x, 110, w, 1, 0x2e3943);
    if (page == 0) {
        int cw = (w - 24) / 3;
        card(x, 135, cw, "用户进程", 2, "独立地址空间");
        card(x + cw + 12, 135, cw, "可用内存", pages_free() * PAGE / 1048576, "MiB / 物理页管理");
        card(x + 2 * cw + 24, 135, cw, "启动自检", test_passed, "项已通过 / 共 8 项");
        text(x, 282, "内核状态", 18, FG);
        line(x, 324, w, "处理器", cpu_brand);
        line(x, 368, w, "调度", "抢占式 / 单核 / 100 Hz");
        line(x, 412, w, "内存保护", "用户态隔离 · W^X · NX");
        line(x, 456, w, "显示与输入", "帧缓冲 · PS/2 · 串口");
        rounded(x, 527, w, 78, 0x23332f);
        text(x + 18, 541, "这是原生内核的早期版本。", 18, ACCENT);
        text(x + 18, 570, "浏览器、CTF 与 Wi-Fi 在 Linux 工具环境中运行。", 18, MUTED);
    } else if (page == 1) {
        card(x, 135, (w - 12) / 2, "已管理物理内存", pages_total() * PAGE / 1048576, "MiB");
        card(x + (w + 12) / 2, 135, (w - 12) / 2, "可分配物理页", pages_free(), "4 KiB / page");
        text(x, 286, "进程", 18, FG);
        text(x + w / 3, 286, "切换次数", 18, MUTED);
        text(x + 2 * w / 3, 286, "用户态计数", 18, MUTED);
        for (unsigned i = 0; i < 2; ++i) {
            int y = 334 + (int)i * 64;
            text(x, y, i ? "01  worker-b" : "00  worker-a", 18, FG);
            numeric(x + w / 3, y, tasks[i].switches, 18, ACCENT);
            numeric(x + 2 * w / 3, y, tasks[i].data ? *(uint64_t *)tasks[i].data : 0, 18, FG);
            rect(x, y + 42, w, 1, 0x2c3641);
        }
        text(x, 498, "两个进程由时钟中断轮流调度，分别使用自己的页表。", 18, MUTED);
        text(x, 536, "内核数据不可从用户态读取，代码页不可写入。", 18, MUTED);
    } else if (page == 2) {
        text(x, 139, "PCI 设备枚举", 18, FG);
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
        text(x, 521, cpu_smep ? "SMEP 已启用" : "CPU 未提供 SMEP", 18, MUTED);
        text(x + w / 2, 521, cpu_smap ? "SMAP 已启用" : "CPU 未提供 SMAP", 18, MUTED);
        text(x, 570, "当前结果仅覆盖启动自检，仍需持续测试与安全审计。", 18, MUTED);
    } else if (page == 4) {
        rounded(x, 135, w, height - 175, 0x10171e);
        text(x + 18, 149, "mnOS kernel console / help 查看命令", 18, ACCENT);
        unsigned max = (height - 265) / 28, start = history_count > max ? history_count - max : 0;
        for (unsigned i = start; i < history_count; ++i)
            text(x + 18, 190 + (int)(i - start) * 28, history[i], 18, FG);
        int cy = height - 81;
        text(x + 18, cy, ">", 18, ACCENT);
        text(x + 38, cy, command, 18, FG);
        text(x + 38 + (int)command_length * 10, cy, "_", 18, ACCENT);
    } else {
        text(x, 146, "Linux 工具环境", 28, FG);
        text(x, 198, "在启动菜单中选择另一个系统入口。", 18, MUTED);
        line(x, 254, w, "浏览器", "Firefox ESR");
        line(x, 302, w, "CTF", "GDB · Pwntools · Nmap");
        line(x, 350, w, "网络与取证", "Wireshark · Hashcat · Binwalk");
        line(x, 398, w, "硬件支持", "常见显卡、网卡与 Wi-Fi 固件");
        rounded(x, 476, 280, 52, 0x31584b);
        text(x + 19, 488, "重启并选择工具环境", 18, ACCENT);
        text(x, 562, "原生内核与 Linux 工具环境是两个独立的运行环境。", 18, MUTED);
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
    if (k >= 0x81 && k <= 0x86) {
        select_page(k - 0x81);
        return;
    }
    if (key == 27) {
        select_page(0);
        return;
    }
    if (page != 4) {
        if (key >= '1' && key <= '6')
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
    cursor_x = x;
    cursor_y = y;
    cursor_dirty = true;
    if (pressed && !left_down && !power_dialog) {
        if (x < sidebar && y >= 130 && y < 460) {
            int n = (y - 130) / 55;
            if (n < 6)
                select_page(n);
        } else if (page == 5 && x >= sidebar + 30 && x < sidebar + 310 && y >= 476 && y < 528) {
            power_dialog = true;
            dirty = true;
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
