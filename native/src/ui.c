/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "auth.h"
#include "kernel.h"
#include "mnfs.h"
#include "process.h"
struct glyph {
    uint32_t code, offset;
    uint16_t width, height, advance, size;
};
extern const struct glyph font_glyphs[];
extern const unsigned font_count;
extern const unsigned char font_pixels[];
static uint32_t canvas[2560 * 1440] __attribute__((aligned(4096)));
static int width, height, page = -1, cursor_x = 640, cursor_y = 360, old_x = 640, old_y = 360;
static bool ready, dirty = true, cursor_dirty, left_down, power_dialog;
static char command[256], history[14][100];
static unsigned command_length, history_count;
static unsigned wallpaper;
static char setting_status[100];
static const unsigned modes[][2] = {
    {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
static const char *wallpapers[] = {"流线", "网格", "纯色"};
static const uint32_t BG = 0x111d28, PANEL = 0x1c2b38, FG = 0xe2ebf0, MUTED = 0x9aacba,
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
    log_write("OUT: ");
    log_write(s);
    log_write("\n");
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
void ui_program_output(const char *s) {
    add_line(s);
    dirty = true;
}
static void setting_message(const char *s) {
    size_t n = strlen(s);
    if (n >= sizeof setting_status)
        n = sizeof setting_status - 1;
    memcpy(setting_status, s, n);
    setting_status[n] = 0;
    dirty = true;
}
static void resolution_text(char *out) {
    number(out, boot.fb.width);
    size_t n = strlen(out);
    out[n++] = 'x';
    number(out + n, boot.fb.height);
}
static bool set_resolution(unsigned w, unsigned h) {
    if (!graphics_mode(w, h)) {
        setting_message("Display mode unavailable on this device.");
        return false;
    }
    ui_init();
    setting_message("分辨率已更新。保存后可在下次启动恢复。");
    return true;
}
static void change_resolution(void) {
    unsigned next = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(modes); ++i)
        if (boot.fb.width == modes[i][0] && boot.fb.height == modes[i][1])
            next = (i + 1) % ARRAY_SIZE(modes);
    for (unsigned attempt = 0; attempt < ARRAY_SIZE(modes); ++attempt) {
        unsigned index = (next + attempt) % ARRAY_SIZE(modes);
        if (set_resolution(modes[index][0], modes[index][1]))
            return;
    }
}
static void change_wallpaper(unsigned n) {
    wallpaper = n;
    setting_message("壁纸已更新。保存后可在下次启动恢复。");
    log_write("SETTINGS: wallpaper=");
    log_dec(wallpaper);
    log_write("\n");
}
static void save_settings(void) {
    unsigned w = boot.fb.width, h = boot.fb.height;
    int e = files_settings(&w, &h, &wallpaper, true);
    setting_message(e == MN_OK      ? "显示设置已保存到个人目录。"
                    : e == MN_RO    ? "磁盘为只读。需要 root 解锁后再保存。"
                    : e == MN_NOENT ? "未挂载 MNFS 磁盘；当前设置在重启后重置。"
                                    : "保存失败；请检查磁盘状态。");
    log_write("SETTINGS: ");
    log_write(e ? mnfs_error(e) : "saved");
    log_write("\n");
}
void ui_load_settings(void) {
    unsigned w, h, background;
    if (files_settings(&w, &h, &background, false) != MN_OK)
        return;
    wallpaper = background;
    if (w == boot.fb.width && h == boot.fb.height)
        dirty = true;
    else if (graphics_mode(w, h))
        ui_init();
    setting_message("已读取 MNFS 磁盘上的显示设置。");
    log_write("SETTINGS: restored wallpaper=");
    log_dec(wallpaper);
    log_write("\n");
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
        add_line(
            "Users: queries, own-home files and display settings. Volume administration: root.");
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
        add_line("F1-F7: apps; Ctrl-S: editor save; kernel help -cn: Chinese.");
    }
}
static void kernel_command(const char *cmd) {
    if (!*cmd || streq(cmd, "help"))
        help(false);
    else if (streq(cmd, "help -cn"))
        help(true);
    else if (streq(cmd, "info")) {
        add_line("mnOS native / " MNOS_ARCH " / original C + assembly kernel");
        add_line(cpu_brand);
        add_number("Online CPUs: ", cpu_online_count());
    } else if (streq(cmd, "cpu")) {
        add_line(cpu_brand);
        add_number("Discovered CPUs: ", cpu_count());
        add_number("Online CPUs: ", cpu_online_count());
    } else if (streq(cmd, "memory")) {
        add_number("Free physical pages (4 KiB): ", pages_free());
        add_number("Usable physical pages (4 KiB): ", pages_total());
        add_number("Physical address limit (bytes): ", memory_limit());
    } else if (streq(cmd, "gpu"))
        add_line(graphics_driver());
    else if (streq(cmd, "devices")) {
        for (unsigned i = 0; i < pci_count; ++i) {
            char row[100];
            pci_read_line(i, row, sizeof row);
            add_line(row);
        }
    } else if (streq(cmd, "test")) {
        add_number("Boot CPU isolation checks: ", test_passed);
        add_line("Per-CPU results are in the serial log.");
    } else if (streq(cmd, "display")) {
        char mode[32];
        resolution_text(mode);
        add_line(mode);
    } else if (!strncmp(cmd, "display ", 8)) {
        bool matched = false;
        for (unsigned i = 0; i < ARRAY_SIZE(modes); ++i) {
            char mode[32];
            number(mode, modes[i][0]);
            size_t n = strlen(mode);
            mode[n++] = 'x';
            number(mode + n, modes[i][1]);
            if (streq(cmd + 8, mode)) {
                matched = true;
                add_line(set_resolution(modes[i][0], modes[i][1]) ? "Display mode applied."
                                                                  : "Display mode unavailable.");
                break;
            }
        }
        if (!matched)
            add_line("Modes: 1024x768 1280x720 1600x900 1920x1080 2560x1440");
    } else if (!strncmp(cmd, "wallpaper ", 10) && cmd[10] >= '0' && cmd[10] <= '2' && !cmd[11]) {
        change_wallpaper(cmd[10] - '0');
        add_line("Wallpaper applied.");
    } else if (streq(cmd, "save-settings")) {
        save_settings();
        add_line(setting_status);
    } else if (!files_command(cmd, add_line))
        add_line("Unknown instruction or invalid object. Type kernel help.");
}
static void execute(void) {
    command[command_length] = 0;
    add_line(command);
    if (auth_command(command, add_line)) {
    } else if (streq(command, "help"))
        help(false);
    else if (streq(command, "help -cn"))
        help(true);
    else if (streq(command, "kernel"))
        kernel_command("");
    else if (!strncmp(command, "kernel ", 7))
        kernel_command(command + 7);
    else if (streq(command, "uname"))
        add_line("mnOS Desktop 0.4 / " MNOS_ARCH " / freestanding C + assembly");
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
    else if (files_command(command, add_line)) {
    } else if (command_length)
        add_line("Unknown command. Type help.");
    log_write("CONSOLE: ");
    log_write(command);
    log_write("\n");
    command_length = 0;
    command[0] = 0;
}
/* Original kernel-resident desktop: independent windows and a shared canvas.
 * Apps use the same authenticated MNFS service as the native file syscall. */
struct window {
    int x, y, w, h;
    bool open, minimized;
};
static struct window windows[7];
static unsigned order[7] = {0, 1, 2, 3, 4, 5, 6};
static bool layout_ready, menu_open, dragging;
static int drag_dx, drag_dy;
static const char *app_names[] = {"Welcome",  "Files",   "Text editor", "System monitor",
                                  "Terminal", "Devices", "Settings"};
static const char *app_desc[] = {"Your native desktop",           "Browse your home folder",
                                 "Create and edit text files",    "Memory and kernel status",
                                 "Commands and native programs",  "Detected hardware",
                                 "Display, wallpaper and session"};
static const uint32_t app_colors[] = {0x83d5bc, 0x6cadf1, 0xe9bb73, 0xb8a5ef,
                                      0x98b7ca, 0x72d6c9, 0xa9b9db};
static char search[40], notice[120];
static unsigned search_len;
struct file_item {
    char name[256];
    struct mnfs_stat st;
};
static struct file_item items[64];
static unsigned item_count, item_scroll;
static int selected = -1;
static char folder[MNFS_PATH], edit_path[MNFS_PATH], edit_buffer[16385];
static size_t edit_size, edit_cursor, edit_scroll;
static bool edit_changed;
static int dialog; /* 1 new text file, 2 new directory, 3 delete, 4 unsaved close */
static char dialog_text[256];
static size_t dialog_len;
static void copy_string(char *out, size_t cap, const char *in) {
    size_t n = strlen(in);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, in, n);
    out[n] = 0;
}
static void announce(const char *s) {
    copy_string(notice, sizeof notice, s);
    dirty = true;
    log_write("DESKTOP: ");
    log_write(s);
    log_write("\n");
}
static bool inside(int x, int y, int a, int b, int w, int h) {
    return x >= a && y >= b && x < a + w && y < b + h;
}
static void icon(int x, int y, unsigned app, bool large) {
    int n = large ? 38 : 20;
    uint32_t c = app_colors[app];
    if (app == 1) {
        rect(x, y + 3, n / 2, 5, c);
        rounded(x, y + 7, n, n * 2 / 3, c);
        rect(x + 3, y + 11, n - 6, 2, 0xaed4fb);
    } else if (app == 4) {
        rounded(x, y, n, n * 3 / 4, 0x456177);
        text(x + 2, y - 6, ">", 18, FG);
        rect(x + n / 2, y + n / 2, n / 4, 2, FG);
    } else {
        rounded(x, y, n, n, c);
        int k = n / 4;
        rect(x + k, y + k, n - 2 * k, 2, 0x23343f);
        rect(x + k, y + k + 5, n - 2 * k, 2, 0x23343f);
        if (large)
            rect(x + k, y + k + 10, n - 2 * k, 2, 0x23343f);
    }
}
static void button(int x, int y, int w, const char *label, bool on) {
    rounded(x, y, w, 32, on ? 0x30584f : 0x2a3b4a);
    text(x + 10, y + 1, label, 18, on ? ACCENT : FG);
}
static void focus_app(int app) {
    page = app;
    windows[app].open = true;
    windows[app].minimized = false;
    unsigned pos = 0;
    while (pos < 7 && order[pos] != (unsigned)app)
        pos++;
    for (unsigned i = pos; i + 1 < 7; i++)
        order[i] = order[i + 1];
    order[6] = app;
    menu_open = false;
    dirty = true;
    log_write("UI: page ");
    log_dec(app);
    log_write("\n");
}
static void home_folder(bool persistent) {
    copy_string(folder, sizeof folder, persistent ? "/disk" : "/ram");
    size_t n = strlen(folder);
    copy_string(folder + n, sizeof folder - n, auth_admin() ? "/root" : "/home/");
    if (!auth_admin()) {
        n = strlen(folder);
        copy_string(folder + n, sizeof folder - n, auth_name());
    }
}
static struct mnfs *gui_volume(const char *full, const char **local, bool write) {
    *local = full;
    struct mnfs *f = files_volume(local);
    return f && auth_path(*local, write) ? f : NULL;
}
static bool list_item(void *unused, const char *name, const struct mnfs_stat *st) {
    (void)unused;
    if (item_count == ARRAY_SIZE(items))
        return false;
    copy_string(items[item_count].name, sizeof items[0].name, name);
    items[item_count++].st = *st;
    return true;
}
static void refresh_files(void) {
    item_count = 0;
    selected = -1;
    item_scroll = 0;
    const char *p;
    struct mnfs *f = gui_volume(folder, &p, false);
    int e = f ? mnfs_list(f, p, list_item, NULL) : MN_NOENT;
    if (e)
        announce(mnfs_error(e));
    else {
        notice[0] = 0;
        dirty = true;
    }
}
static bool child_path(char *path, const char *name) {
    size_t a = strlen(folder), b = strlen(name);
    if (a + b + 2 > MNFS_PATH)
        return false;
    memcpy(path, folder, a);
    path[a++] = '/';
    memcpy(path + a, name, b + 1);
    return true;
}
static void open_file(unsigned index) {
    static char loaded_text[sizeof edit_buffer];
    if (index >= item_count)
        return;
    char path[MNFS_PATH];
    if (!child_path(path, items[index].name))
        return;
    if (items[index].st.directory) {
        copy_string(folder, sizeof folder, path);
        refresh_files();
        return;
    }
    if (edit_changed) {
        announce("Save your current document before opening another file.");
        focus_app(2);
        return;
    }
    if (items[index].st.size > sizeof edit_buffer - 1) {
        announce("This editor opens text files up to 16 KiB.");
        return;
    }
    const char *local;
    struct mnfs *f = gui_volume(path, &local, false);
    size_t got = 0;
    int e = f ? mnfs_read(f, local, 0, loaded_text, sizeof loaded_text - 1, &got) : MN_RO;
    if (e) {
        announce(mnfs_error(e));
        return;
    }
    for (size_t i = 0; i < got; i++)
        if ((unsigned char)loaded_text[i] < 32 && loaded_text[i] != '\n' &&
            loaded_text[i] != '\t' && loaded_text[i] != '\r') {
            announce("Binary file: open it from the terminal.");
            return;
        }
    memcpy(edit_buffer, loaded_text, got);
    auth_wipe(loaded_text, sizeof loaded_text);
    edit_buffer[got] = 0;
    edit_size = got;
    edit_cursor = got;
    edit_scroll = 0;
    copy_string(edit_path, sizeof edit_path, path);
    focus_app(2);
    log_write("EDITOR: opened ");
    log_write(edit_path);
    log_write("\n");
}
static void save_document(void) {
    if (!*edit_path) {
        dialog = 5;
        dialog_len = 0;
        dialog_text[0] = 0;
        dirty = true;
        return;
    }
    const char *local;
    struct mnfs *f = gui_volume(edit_path, &local, true);
    int e = f ? MN_OK : MN_RO;
    struct mnfs_stat st;
    if (!e) {
        e = mnfs_stat(f, local, &st);
        if (e == MN_NOENT)
            e = mnfs_create(f, local, false);
    }
    if (!e)
        e = mnfs_write(f, local, 0, edit_buffer, edit_size);
    if (!e)
        e = mnfs_truncate(f, local, edit_size);
    if (e)
        announce(mnfs_error(e));
    else {
        edit_changed = false;
        refresh_files();
        announce("Document saved and flushed to disk.");
        log_write("EDITOR: saved ");
        log_write(edit_path);
        log_write("\n");
    }
}
static void begin_name(bool directory) {
    if (!directory && edit_changed) {
        announce("Save the open document before creating another.");
        return;
    }
    dialog = directory ? 2 : 1;
    dialog_len = 0;
    dialog_text[0] = 0;
    dirty = true;
}
static void finish_dialog(void) {
    if (dialog == 4) {
        auth_wipe(edit_buffer, sizeof edit_buffer);
        edit_path[0] = 0;
        edit_size = edit_cursor = edit_scroll = 0;
        edit_changed = false;
        windows[2].open = false;
        page = -1;
        dialog = 0;
        dirty = true;
        return;
    }
    if (dialog == 3) {
        if (selected >= 0) {
            char path[MNFS_PATH];
            const char *p;
            if (child_path(path, items[selected].name)) {
                struct mnfs *f = gui_volume(path, &p, true);
                int e = f ? mnfs_remove(f, p) : MN_RO;
                if (e)
                    announce(mnfs_error(e));
                else
                    refresh_files();
            }
        }
        dialog = 0;
        dirty = true;
        return;
    }
    if (!dialog_len || streq(dialog_text, ".") || streq(dialog_text, "..")) {
        announce("Enter a file name.");
        return;
    }
    for (size_t i = 0; i < dialog_len; i++)
        if (dialog_text[i] == '/') {
            announce("A name cannot contain '/'.");
            return;
        }
    char path[MNFS_PATH];
    const char *p;
    if (!child_path(path, dialog_text)) {
        announce("Path is too long.");
        return;
    }
    struct mnfs *f = gui_volume(path, &p, true);
    int e = f ? mnfs_create(f, p, dialog == 2) : MN_RO;
    if (e) {
        announce(mnfs_error(e));
        return;
    }
    if (dialog == 5) {
        copy_string(edit_path, sizeof edit_path, path);
        dialog = 0;
        save_document();
        return;
    }
    if (dialog == 1) {
        edit_buffer[0] = 0;
        edit_size = edit_cursor = edit_scroll = 0;
        edit_changed = false;
        copy_string(edit_path, sizeof edit_path, path);
        focus_app(2);
    }
    dialog = 0;
    refresh_files();
    dirty = true;
}
static void folder_up(void) {
    char original[MNFS_PATH];
    copy_string(original, sizeof original, folder);
    size_t n = strlen(folder);
    while (n && folder[n - 1] != '/')
        n--;
    if (n > 0)
        folder[n - 1] = 0;
    const char *p;
    if (!gui_volume(folder, &p, false) || !strcmp(folder, "/disk") || !strcmp(folder, "/ram"))
        copy_string(folder, sizeof folder, original);
    refresh_files();
}
static bool search_match(const char *s) {
    if (!search_len)
        return true;
    for (; *s; s++) {
        unsigned j = 0;
        while (j < search_len && s[j]) {
            char c = s[j];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c != search[j])
                break;
            j++;
        }
        if (j == search_len)
            return true;
    }
    return false;
}
static void background(void) {
    for (int y = 36; y < height; y++)
        for (int x = 0; x < width; x++) {
            unsigned depth =
                (unsigned)((int64_t)(width - x) * (height - y) * 54 / (width * height));
            uint32_t c = wallpaper == 2
                             ? BG
                             : ((12 + depth / 5) << 16) | ((25 + depth / 2) << 8) | (38 + depth);
            canvas[y * width + x] = c;
        }
    if (wallpaper == 0)
        for (int n = 0; n < 8; n++)
            for (int x = 0; x < width; x++) {
                int64_t dx = x - width / 2,
                        yf = ((int64_t)height / 2 + 120 + n * 27) * 256 - dx * dx * 256 / 5040;
                int y = (int)(yf / 256);
                unsigned alpha = (unsigned)(yf & 255);
                uint32_t c = n % 2 ? 0x5184a0 : 0x79b7a7;
                if (y >= 36 && y + 1 < height) {
                    pixel(x, y, blend(c, canvas[y * width + x], (255 - alpha) * 85 / 255));
                    pixel(x, y + 1, blend(c, canvas[(y + 1) * width + x], alpha * 85 / 255));
                }
            }
    if (wallpaper == 1) {
        for (int x = 0; x < width; x += 48)
            rect(x, 36, 1, height - 36, 0x203746);
        for (int y = 36; y < height; y += 48)
            rect(0, y, width, 1, 0x203746);
    }
    text(width - 215, height - 92, "mnOS", 28, 0x86aaaf);
    text(width - 215, height - 48, "NATIVE DESKTOP", 18, 0x648490);
    const unsigned shortcuts[] = {1, 2, 4, 6};
    for (unsigned i = 0; i < 4; i++) {
        int y = 77 + (int)i * 98;
        icon(33, y, shortcuts[i], true);
        text(21, y + 40, app_names[shortcuts[i]], 18, FG);
    }
}
static void editor_content(struct window *v) {
    int x = v->x + 18, y = v->y + 47, w = v->w - 36;
    button(x, y, 75, "New", false);
    button(x + 85, y, 85, "Save", edit_changed);
    text(x + 185, y + 1, edit_changed ? "Unsaved changes" : "Saved", 18,
         edit_changed ? 0xe9bb73 : MUTED);
    char path[75];
    copy_string(path, sizeof path, *edit_path ? edit_path : "New document");
    text(x, y + 40, path, 18, MUTED);
    rounded(x, y + 77, w, v->h - 155, 0x101b25);
    unsigned cols = (unsigned)(w - 36) / 11, rows = (unsigned)(v->h - 178) / 25;
    if (!cols)
        cols = 1;
    size_t line = 0, col = 0, cursor_line = 0;
    for (size_t i = 0; i < edit_cursor; i++) {
        if (edit_buffer[i] == '\n' || ++col >= cols) {
            line++;
            col = 0;
        }
    }
    cursor_line = line;
    if (cursor_line < edit_scroll)
        edit_scroll = cursor_line;
    if (cursor_line >= edit_scroll + rows)
        edit_scroll = cursor_line - rows + 1;
    line = col = 0;
    for (size_t i = 0; i <= edit_size; i++) {
        if (line >= edit_scroll && line < edit_scroll + rows) {
            int xx = x + 14 + (int)col * 11, yy = y + 82 + (int)(line - edit_scroll) * 25;
            if (i == edit_cursor)
                rect(xx, yy + 5, 2, 20, ACCENT);
            if (i < edit_size && edit_buffer[i] != '\n') {
                char c[2] = {edit_buffer[i] == '\t' ? ' ' : edit_buffer[i], 0};
                text(xx, yy, c, 18, FG);
            }
        }
        if (i < edit_size && (edit_buffer[i] == '\n' || ++col >= cols)) {
            line++;
            col = 0;
        }
    }
    text(x, v->y + v->h - 32, "Ctrl-S save   Ctrl-N new   Arrow keys move   Esc desktop", 18,
         MUTED);
}
static void window_content(unsigned app) {
    struct window *v = &windows[app];
    int x = v->x + 20, y = v->y + 52, w = v->w - 40;
    rect(v->x + 5, v->y + 9, v->w, v->h, 0x0b151e);
    rounded(v->x, v->y, v->w, v->h, PANEL);
    rect(v->x + 1, v->y + 35, v->w - 2, 1, 0x354452);
    text(x, v->y + 2, app_names[app], 18, page == (int)app ? FG : MUTED);
    text(v->x + v->w - 78, v->y + 2, "_", 18, MUTED);
    text(v->x + v->w - 37, v->y + 2, "x", 18, 0xefb4ab);
    if (app == 0) {
        text(x + 8, y + 8, "A desktop of our own.", 28, FG);
        text(x + 8, y + 60, "mnOS / original C and assembly kernel", 18, ACCENT);
        text(x + 8, y + 109, "Open files. Write a note. Change your workspace.", 18, MUTED);
        const unsigned quick[] = {1, 2, 4, 6};
        for (unsigned i = 0; i < 4; i++) {
            int xx = x + (i % 2) * (w / 2), yy = y + 165 + (i / 2) * 96;
            rounded(xx, yy, w / 2 - 12, 82, 0x253746);
            icon(xx + 14, yy + 22, quick[i], true);
            text(xx + 68, yy + 12, app_names[quick[i]], 18, FG);
            text(xx + 68, yy + 40,
                 i == 0   ? "Your saved documents"
                 : i == 1 ? "A simple native editor"
                 : i == 2 ? "help / help -cn"
                          : "Make it yours",
                 18, MUTED);
        }
        const char *p = "/disk";
        bool installed = files_volume(&p) != NULL;
        text(x, y + 391,
             installed ? "Storage: persistent MNFS volume mounted"
                       : "Storage: temporary session. Use the installer to keep files.",
             18, installed ? ACCENT : 0xe9bb73);
    } else if (app == 1) {
        button(x, y, 72, "Home", false);
        button(x + 82, y, 64, "Up", false);
        button(x + 156, y, 92, "New file", false);
        button(x + 258, y, 104, "New folder", false);
        button(x + 372, y, 82, "Delete", false);
        char path[75];
        copy_string(path, sizeof path, folder);
        text(x, y + 40, path, 18, ACCENT);
        text(x + 12, y + 81, "NAME", 18, MUTED);
        text(x + w - 180, y + 81, "SIZE", 18, MUTED);
        rect(x, y + 113, w, 1, 0x354452);
        unsigned visible = (unsigned)(v->h - 212) / 35;
        for (unsigned i = item_scroll; i < item_count && i < item_scroll + visible; i++) {
            int yy = y + 120 + (int)(i - item_scroll) * 35;
            if ((int)i == selected)
                rounded(x, yy, w, 34, 0x31534d);
            icon(x + 10, yy + 7, items[i].st.directory ? 1 : 2, false);
            char name[65];
            copy_string(name, sizeof name, items[i].name);
            text(x + 42, yy + 1, name, 18, FG);
            if (items[i].st.directory)
                text(x + w - 180, yy + 1, "Folder", 18, MUTED);
            else
                numeric(x + w - 180, yy + 1, items[i].st.size, 18, MUTED);
        }
        if (!item_count)
            text(x + 12, y + 135, "This folder is empty. Create your first file.", 18, MUTED);
        text(x, v->y + v->h - 33, "Click a file to open. N new file / D folder / Backspace up", 18,
             MUTED);
    } else if (app == 2)
        editor_content(v);
    else if (app == 3) {
        int cw = (w - 20) / 3;
        card(x, y, cw, "Online CPUs", cpu_online_count(), "detected at startup");
        card(x + cw + 10, y, cw, "Free RAM", pages_free() * PAGE / (1 << 20), "MiB");
        card(x + 2 * (cw + 10), y, cw, "Boot checks", test_passed, "passed / 8");
        line(x, y + 145, w, "Kernel", "mnOS original / " MNOS_ARCH);
        line(x, y + 192, w, "User", auth_name());
        line(x, y + 239, w, "Protection", "Separate page tables / W^X / NX");
        line(x, y + 286, w, "File system", "MNFS / CoW / CRC32C");
        text(x, y + 353, "Native processes: use ps and exec in Terminal.", 18, MUTED);
        text(x, y + 390, "Desktop services currently run inside the kernel.", 18, MUTED);
    } else if (app == 4) {
        rounded(x, y, w, v->h - 87, 0x0c1720);
        text(x + 14, y + 5, auth_name(), 18, ACCENT);
        unsigned max = (unsigned)(v->h - 174) / 25,
                 start = history_count > max ? history_count - max : 0;
        for (unsigned i = start; i < history_count; i++) {
            char row[105];
            unsigned limit = (unsigned)(w - 28) / 10;
            if (limit > 99)
                limit = 99;
            copy_string(row, limit + 1, history[i]);
            text(x + 14, y + 40 + (int)(i - start) * 25, row, 18, FG);
        }
        text(x + 14, v->y + v->h - 61, ">", 18, ACCENT);
        size_t view = (size_t)(w - 65) / 11;
        const char *visible = command_length > view ? command + command_length - view : command;
        text(x + 36, v->y + v->h - 61, visible, 18, FG);
    } else if (app == 5) {
        text(x, y, "Detected devices", 28, FG);
        text(x, y + 51, graphics_driver(), 18, ACCENT);
        for (unsigned i = 0; i < pci_count && i < 8; i++) {
            char row[100];
            pci_read_line(i, row, sizeof row);
            text(x, y + 98 + (int)i * 34, row, 18, MUTED);
        }
        text(x, y + 390, "A detected device requires a compatible native driver.", 18, MUTED);
    } else {
        text(x, y, "Display and workspace", 28, FG);
        char mode[32];
        resolution_text(mode);
        line(x, y + 62, w, "Display", graphics_driver());
        button(x, y + 116, 235, "Resolution / R", false);
        text(x + 264, y + 116, mode, 18, FG);
        button(x, y + 168, 235, "Wallpaper / W", false);
        text(x + 264, y + 168, wallpapers[wallpaper], 18, FG);
        button(x, y + 220, 235, "Save settings / S", true);
        button(x, y + 291, 145, "Lock session", false);
        button(x + 160, y + 291, 145, "Power", false);
        text(x, y + 349, setting_status, 18, MUTED);
        text(x, y + 390, "Saved settings belong to your account.", 18, MUTED);
    }
}
static void draw(void) {
    background();
    if (auth_prompting()) {
        int w = width > 850 ? 780 : width - 50, x = (width - w) / 2, y = (height - 370) / 2;
        rounded(x, y, w, 370, PANEL);
        text(x + 28, y + 17, "mnOS / Welcome", 28, FG);
        text(x + 28, y + 75, auth_status(), 18, MUTED);
        text(x + 28, y + 131, auth_prompt(), 18, ACCENT);
        rounded(x + 28, y + 185, w - 56, 52, 0x101b25);
        text(x + 42, y + 194, auth_input(), 18, FG);
        text(x + 28, y + 267, "Enter to continue     Esc to return to sign in", 18, MUTED);
        display_rect(0, 0, width, height);
        dirty = false;
        return;
    }
    for (unsigned i = 0; i < 7; i++)
        if (windows[order[i]].open && !windows[order[i]].minimized)
            window_content(order[i]);
    rect(0, 0, width, 36, 0x101923);
    text(14, 1, "m  Applications", 18, ACCENT);
    unsigned slot = 0;
    for (unsigned i = 0; i < 7; i++)
        if (windows[i].open) {
            int x = 185 + (int)slot++ * 142;
            if (page == (int)i && !windows[i].minimized)
                rect(x, 1, 138, 34, 0x293b47);
            text(x + 9, 1, app_names[i], 18, FG);
        }
    text(width - 185, 1, auth_name(), 18, MUTED);
    numeric(width - 72, 1, ticks / PIT_HZ, 18, MUTED);
    if (menu_open) {
        rounded(8, 44, 370, 525, 0x20313f);
        text(26, 54, "Applications", 28, FG);
        rounded(22, 106, 341, 38, 0x101c28);
        text(33, 110, search_len ? search : "Type to search...", 18, search_len ? FG : MUTED);
        unsigned row = 0;
        for (unsigned i = 0; i < 7; i++)
            if (search_match(app_names[i])) {
                int y = 160 + (int)row++ * 51;
                icon(27, y + 7, i, false);
                text(64, y, app_names[i], 18, FG);
                text(64, y + 23, app_desc[i], 18, MUTED);
            }
        text(25, 529, "Esc close / Enter open first result", 18, MUTED);
    }
    if (*notice) {
        rounded(12, height - 49, width - 24, 38, 0x2c414a);
        text(24, height - 47, notice, 18, FG);
    }
    if (power_dialog || dialog) {
        int x = (width - 650) / 2, y = (height - 230) / 2;
        rounded(x, y, 650, 230, 0x304454);
        text(x + 24, y + 15,
             power_dialog  ? "Power"
             : dialog == 5 ? "Save document"
             : dialog == 1 ? "New text file"
             : dialog == 2 ? "New folder"
             : dialog == 3 ? "Delete selected item?"
                           : "Discard unsaved changes?",
             28, FG);
        if (power_dialog)
            text(x + 24, y + 81, "R restart   S power off   Esc cancel", 18, FG);
        else if (dialog < 3 || dialog == 5) {
            rounded(x + 24, y + 80, 600, 45, 0x101b25);
            text(x + 36, y + 85, dialog_text, 18, FG);
            text(x + 24, y + 146, "Enter create / Esc cancel", 18, MUTED);
        } else
            text(x + 24, y + 81, "Enter confirms / Esc keeps your files", 18, FG);
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
    if (width > 2560 || height > 1440)
        return;
    int w = width > 1100 ? 900 : width - 110, h = height > 740 ? 590 : height - 104;
    for (unsigned i = 0; i < 7; i++) {
        windows[i].x = (width - w) / 2 + (int)(i % 3) * 12;
        windows[i].y = 58 + (int)(i % 3) * 16;
        windows[i].w = w;
        windows[i].h = h;
    }
    if (!layout_ready) {
        windows[0].open = true;
        page = 0;
        layout_ready = true;
    }
    ready = true;
    cursor_x = width / 2;
    cursor_y = height / 2;
    dirty = true;
    draw();
}
void ui_tick(void) {
    if (!ready)
        return;
    auth_tick(ticks / PIT_HZ);
    process_session_check();
    static uint32_t last_user = UINT32_MAX;
    if (auth_uid() != last_user) {
        last_user = auth_uid();
        history_count = 0;
        auth_wipe(command, sizeof command);
        command_length = 0;
        auth_wipe(edit_buffer, sizeof edit_buffer);
        edit_size = edit_cursor = edit_scroll = 0;
        edit_changed = false;
        edit_path[0] = 0;
        wallpaper = 0;
        power_dialog = false;
        menu_open = false;
        dialog = 0;
        notice[0] = 0;
        for (unsigned i = 0; i < 7; i++) {
            windows[i].open = false;
            windows[i].minimized = false;
        }
        if (auth_logged_in()) {
            files_session();
            ui_load_settings();
            const char *p = "/disk";
            home_folder(files_volume(&p) != NULL);
            refresh_files();
            focus_app(0);
            log_write("DESKTOP READY: original native GUI / ");
            log_write(MNOS_ARCH);
            log_write("\n");
        }
        dirty = true;
    }
    static const char *last_prompt, *last_status;
    if (last_prompt != auth_prompt() || last_status != auth_status()) {
        last_prompt = auth_prompt();
        last_status = auth_status();
        dirty = true;
        log_write("AUTH: ");
        log_write(last_status);
        log_write("\nAUTH PROMPT: ");
        log_write(last_prompt);
        log_write("\n");
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
void ui_key(char key) {
    if (auth_prompting()) {
        auth_key(key);
        dirty = true;
        return;
    }
    unsigned char k = (unsigned char)key;
    dirty = true;
    if (power_dialog) {
        if (key == 'r' || key == 'R')
            reboot();
        else if (key == 's' || key == 'S')
            power_off();
        else if (key == 27)
            power_dialog = false;
        return;
    }
    if (dialog) {
        if (key == 27)
            dialog = 0;
        else if (key == '\n' || key == '\r')
            finish_dialog();
        else if (key == '\b' || key == 127) {
            if (dialog_len)
                dialog_text[--dialog_len] = 0;
        } else if ((dialog < 3 || dialog == 5) && key >= 32 && key < 127 && dialog_len < sizeof dialog_text - 1) {
            dialog_text[dialog_len++] = key;
            dialog_text[dialog_len] = 0;
        }
        return;
    }
    if (k >= 0x81 && k <= 0x87) {
        focus_app(k - 0x81);
        if (page == 1)
            refresh_files();
        return;
    }
    if (menu_open) {
        if (key == 27)
            menu_open = false;
        else if (key == '\n' || key == '\r') {
            for (unsigned i = 0; i < 7; i++)
                if (search_match(app_names[i])) {
                    focus_app(i);
                    if (page == 1)
                        refresh_files();
                    break;
                }
        } else if (key == '\b' || key == 127) {
            if (search_len)
                search[--search_len] = 0;
        } else if (key >= 32 && key < 127 && search_len < sizeof search - 1) {
            search[search_len++] = key >= 'A' && key <= 'Z' ? key + 32 : key;
            search[search_len] = 0;
        }
        return;
    }
    if (key == 27) {
        page = -1;
        for (unsigned i = 0; i < 7; i++)
            windows[i].minimized = true;
        return;
    }
    if (page == 2) {
        if (k == 19) {
            save_document();
            return;
        }
        if (k == 14) {
            begin_name(false);
            return;
        }
        if (k == 0x90) {
            if (edit_cursor)
                edit_cursor--;
            return;
        }
        if (k == 0x91) {
            if (edit_cursor < edit_size)
                edit_cursor++;
            return;
        }
        if (k == 0x92 || k == 0x93) {
            size_t col = 0, p = edit_cursor;
            while (p && edit_buffer[p - 1] != '\n') {
                p--;
                col++;
            }
            if (k == 0x92 && p) {
                size_t end = p - 1, start = end;
                while (start && edit_buffer[start - 1] != '\n')
                    start--;
                edit_cursor = start + (col < end - start ? col : end - start);
            } else if (k == 0x93) {
                size_t end = edit_cursor;
                while (end < edit_size && edit_buffer[end] != '\n')
                    end++;
                if (end < edit_size) {
                    size_t start = ++end;
                    while (end < edit_size && edit_buffer[end] != '\n')
                        end++;
                    edit_cursor = start + (col < end - start ? col : end - start);
                }
            }
            return;
        }
        if (key == '\b' || key == 127) {
            if (edit_cursor) {
                for (size_t i = edit_cursor; i <= edit_size; i++)
                    edit_buffer[i - 1] = edit_buffer[i];
                edit_cursor--;
                edit_size--;
                edit_changed = true;
            }
        } else if ((key >= 32 && key < 127) || key == '\n' || key == '\r' || key == '\t') {
            if (edit_size >= sizeof edit_buffer - 1) {
                announce("Document reached the editor's 16 KiB capacity.");
                return;
            }
            for (size_t i = edit_size + 1; i > edit_cursor; i--)
                edit_buffer[i] = edit_buffer[i - 1];
            edit_buffer[edit_cursor++] = key == '\r' ? '\n' : key;
            edit_size++;
            edit_changed = true;
        }
        return;
    }
    if (page == 4) {
        if (key == '\n' || key == '\r')
            execute();
        else if (key == '\b' || key == 127) {
            if (command_length)
                command[--command_length] = 0;
        } else if (key >= 32 && key < 127 && command_length < sizeof command - 1) {
            command[command_length++] = key;
            command[command_length] = 0;
        }
        return;
    }
    if (page == 1) {
        if (key == 'n')
            begin_name(false);
        else if (key == 'd')
            begin_name(true);
        else if (key == '\b')
            folder_up();
        else if (key == '\n' && selected >= 0)
            open_file((unsigned)selected);
        else if (k == 0x92 || k == 0x93) {
            if (item_count) {
                if (k == 0x92 && selected > 0)
                    selected--;
                if (k == 0x93 && selected + 1 < (int)item_count)
                    selected++;
                if (selected < (int)item_scroll)
                    item_scroll = (unsigned)selected;
                unsigned visible = (unsigned)(windows[1].h - 212) / 35;
                if (selected >= (int)(item_scroll + visible))
                    item_scroll = (unsigned)selected - visible + 1;
            }
        }
    }
    if (page == 6) {
        if (key == 'r')
            change_resolution();
        else if (key == 'w')
            change_wallpaper((wallpaper + 1) % ARRAY_SIZE(wallpapers));
        else if (key == 's')
            save_settings();
    }
    if (key == 't')
        focus_app(4);
    else if (key == 'f') {
        focus_app(1);
        refresh_files();
    } else if (key == 'e')
        focus_app(2);
    else if (key == 'p')
        power_dialog = true;
    else if (key == 'm') {
        menu_open = true;
        search_len = 0;
        search[0] = 0;
    }
}
void ui_pointer(int x, int y, bool pressed) {
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= width)
        x = width - 1;
    if (y >= height)
        y = height - 1;
    cursor_x = x;
    cursor_y = y;
    cursor_dirty = true;
    if (auth_prompting()) {
        left_down = pressed;
        return;
    }
    if (dragging) {
        if (!pressed)
            dragging = false;
        else if (page >= 0) {
            struct window *v = &windows[page];
            v->x = x - drag_dx;
            v->y = y - drag_dy;
            if (v->x < 0)
                v->x = 0;
            if (v->x + v->w > width)
                v->x = width - v->w;
            if (v->y < 37)
                v->y = 37;
            if (v->y + v->h > height)
                v->y = height - v->h;
            dirty = true;
        }
    }
    if (pressed && !left_down && !power_dialog && !dialog) {
        if (y < 36) {
            if (x < 180) {
                menu_open = !menu_open;
                search_len = 0;
                search[0] = 0;
                dirty = true;
                if (menu_open)
                    log_write("DESKTOP: launcher opened by pointer\n");
            } else {
                unsigned slot = 0;
                for (unsigned i = 0; i < 7; i++)
                    if (windows[i].open) {
                        if (inside(x, y, 185 + (int)slot * 142, 0, 138, 36)) {
                            focus_app(i);
                            break;
                        }
                        slot++;
                    }
            }
        } else if (menu_open) {
            unsigned row = 0;
            bool found = false;
            for (unsigned i = 0; i < 7; i++)
                if (search_match(app_names[i])) {
                    if (inside(x, y, 20, 160 + (int)row * 51, 340, 51)) {
                        focus_app(i);
                        if (page == 1)
                            refresh_files();
                        found = true;
                        break;
                    }
                    row++;
                }
            if (!found && !inside(x, y, 8, 44, 370, 525))
                menu_open = false;
            dirty = true;
        } else {
            int hit = -1;
            for (int z = 6; z >= 0; z--) {
                unsigned a = order[z];
                struct window *v = &windows[a];
                if (v->open && !v->minimized && inside(x, y, v->x, v->y, v->w, v->h)) {
                    hit = (int)a;
                    break;
                }
            }
            if (hit < 0) {
                const unsigned apps[] = {1, 2, 4, 6};
                for (unsigned i = 0; i < 4; i++)
                    if (inside(x, y, 15, 72 + (int)i * 98, 100, 90)) {
                        focus_app(apps[i]);
                        if (page == 1)
                            refresh_files();
                    }
            } else {
                focus_app(hit);
                struct window *v = &windows[hit];
                int xx = x - v->x - 20, yy = y - v->y - 52;
                if (y < v->y + 35) {
                    if (x >= v->x + v->w - 48) {
                        if (hit == 2 && edit_changed)
                            dialog = 4;
                        else {
                            v->open = false;
                            page = -1;
                        }
                    } else if (x >= v->x + v->w - 90) {
                        v->minimized = true;
                        page = -1;
                    } else {
                        dragging = true;
                        drag_dx = x - v->x;
                        drag_dy = y - v->y;
                    }
                } else if (hit == 0) {
                    const unsigned a[] = {1, 2, 4, 6};
                    for (unsigned i = 0; i < 4; i++)
                        if (inside(xx, yy, (int)(i % 2) * ((v->w - 40) / 2),
                                   165 + (int)(i / 2) * 96, (v->w - 40) / 2 - 12, 82)) {
                            focus_app(a[i]);
                            if (page == 1)
                                refresh_files();
                            break;
                        }
                } else if (hit == 1) {
                    if (yy >= 0 && yy < 32) {
                        if (xx < 72) {
                            const char *p = "/disk";
                            home_folder(files_volume(&p) != NULL);
                            refresh_files();
                        } else if (xx < 146)
                            folder_up();
                        else if (xx < 248)
                            begin_name(false);
                        else if (xx < 362)
                            begin_name(true);
                        else if (xx < 454 && selected >= 0)
                            dialog = 3;
                    } else if (yy >= 120 && y < v->y + v->h - 45) {
                        unsigned i = item_scroll + (unsigned)(yy - 120) / 35;
                        if (i < item_count) {
                            selected = (int)i;
                            open_file(i);
                        }
                    }
                } else if (hit == 2 && yy >= -5 && yy < 30) {
                    if (xx < 75)
                        begin_name(false);
                    else if (xx < 170)
                        save_document();
                } else if (hit == 6) {
                    if (yy >= 116 && yy < 148 && xx < 235)
                        change_resolution();
                    else if (yy >= 168 && yy < 200 && xx < 235)
                        change_wallpaper((wallpaper + 1) % 3);
                    else if (yy >= 220 && yy < 252 && xx < 235)
                        save_settings();
                    else if (yy >= 291 && yy < 323) {
                        if (xx < 145)
                            auth_lock();
                        else if (xx < 305)
                            power_dialog = true;
                    }
                }
                dirty = true;
            }
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
