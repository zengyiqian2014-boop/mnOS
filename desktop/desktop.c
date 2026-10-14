/* A small framebuffer launcher; no display server required.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "font8x8.h"

struct rect { int x, y, w, h; };
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static uint8_t *fb;
static uint32_t *canvas;
static int width, height, tty = -1, mouse = -1, page, selected;
static int mx, my, old_mode, have_term, have_mode;
static struct termios saved_term, raw_term;
static volatile sig_atomic_t running = 1;
static struct rect cards[4];
static unsigned long mem_total, mem_available;
static long uptime_seconds;
static char kernel[96];

enum { HOME, SYSTEM, HELP, POWER };
enum { BG = 0x101820, PANEL = 0x1b2631, LINE = 0x344351,
       INK = 0xe2e8ec, MUTED = 0x96a7b5, ACCENT = 0x72cfba };

static void stop(int sig) { (void)sig; running = 0; }
static int inside(struct rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static uint32_t channel(unsigned value, struct fb_bitfield field) {
    if (!field.length) return 0;
    return (value * ((1u << field.length) - 1) / 255) << field.offset;
}
static void box(int x, int y, int w, int h, uint32_t color) {
    color = channel((color >> 16) & 255, var.red) |
            channel((color >> 8) & 255, var.green) |
            channel(color & 255, var.blue) | channel(255, var.transp);
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int right = x + w > width ? width : x + w;
    int bottom = y + h > height ? height : y + h;
    for (int yy = top; yy < bottom; yy++)
        for (int xx = left; xx < right; xx++) canvas[yy * width + xx] = color;
}
static void text(int x, int y, const char *s, int scale, uint32_t color) {
    for (; *s; s++, x += 8 * scale) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 128) ch = '?';
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (font8x8_basic[ch][row] & (1u << col))
                    box(x + col * scale, y + row * scale, scale, scale, color);
    }
}
static void present(void) {
    unsigned bytes = var.bits_per_pixel / 8;
    for (int y = 0; y < height; y++) {
        uint8_t *row = fb + (y + var.yoffset) * fix.line_length + var.xoffset * bytes;
        if (bytes == 4) {
            memcpy(row, canvas + y * width, (size_t)width * 4);
            continue;
        }
        for (int x = 0; x < width; x++) {
            uint32_t pixel = canvas[y * width + x];
            if (bytes == 2) memcpy(row + x * 2, &pixel, 2);
            else memcpy(row + x * 3, &pixel, 3);
        }
    }
}
static void metrics(void) {
    char line[160];
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            unsigned long value;
            if (sscanf(line, "MemTotal: %lu", &value) == 1) mem_total = value;
            if (sscanf(line, "MemAvailable: %lu", &value) == 1) mem_available = value;
        }
        fclose(f);
    }
    f = fopen("/proc/uptime", "r");
    if (f) {
        double seconds;
        if (fscanf(f, "%lf", &seconds) == 1) uptime_seconds = (long)seconds;
        fclose(f);
    }
}
static void cursor(void) {
    if (mouse < 0) return;
    for (int y = 0; y < 14; y++) box(mx, my + y, y / 2 + 1, 1, 0x05090c);
    for (int y = 1; y < 11; y++) box(mx + 1, my + y, y / 2, 1, INK);
}
static void draw(void) {
    int scale = width >= 900 ? 2 : 1;
    int left = width >= 900 ? 220 : 156;
    int content = left + 32, available = width - content - 32;
    char line[160], clock_text[32];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(clock_text, sizeof(clock_text), "%H:%M", tm);
    box(0, 0, width, height, BG);
    box(0, 0, width, 64, PANEL);
    box(0, 64, width, 1, LINE);
    text(28, 20, "mnOS", 3, INK);
    if (mouse < 0) text(width - 270, 28, "Keyboard input", 1, MUTED);
    text(width - 110, 25, clock_text, 2, MUTED);
    box(left, 88, 1, height - 150, LINE);
    text(28, 103, "WORKSPACE", 1, MUTED);
    const char *navigation[] = {"Overview", "System", "Help", "Power"};
    for (int i = 0; i < 4; i++) {
        int y = 140 + i * 52;
        if (page == i) {
            box(16, y - 10, left - 32, 38, PANEL);
            box(16, y - 10, 3, 38, ACCENT);
        }
        text(30, y, navigation[i], scale, page == i ? INK : MUTED);
    }
    text(28, height - 104, "LOCAL SESSION", 1, MUTED);
    text(28, height - 82, "root", scale, ACCENT);
    box(0, height - 40, width, 1, LINE);
    text(24, height - 24, "T Terminal   S System   H Help   P Power", 1, MUTED);
    text(width - 136, height - 24, "Esc Back / Exit", 1, MUTED);

    if (page == HOME) {
        text(content, 106, "Your workspace", scale + 1, INK);
        text(content, 148, "A small system. Ready to work.", scale, MUTED);
        const char *titles[] = {"Terminal", "System", "Help", "Power"};
        const char *subtitles[] = {"Open a shell", "Memory and uptime", "Commands and keys", "Shut down or reboot"};
        const char *icons[] = {">_", "||", "?", "O"};
        int cw = (available - 20) / 2;
        int ch = (height - 278) / 2;
        if (ch > 170) ch = 170;
        for (int i = 0; i < 4; i++) {
            struct rect r = {content + (i % 2) * (cw + 20), 204 + (i / 2) * (ch + 18), cw, ch};
            cards[i] = r;
            box(r.x, r.y, r.w, r.h, i == selected ? LINE : PANEL);
            box(r.x, r.y, 3, r.h, i == selected ? ACCENT : PANEL);
            text(r.x + 20, r.y + 18, icons[i], 3, ACCENT);
            text(r.x + 20, r.y + 60, titles[i], scale, INK);
            text(r.x + 20, r.y + 88, subtitles[i], 1, MUTED);
        }
    } else if (page == SYSTEM) {
        text(content, 106, "System", scale + 1, INK);
        text(content, 155, kernel, scale, MUTED);
        box(content, 200, available, 114, PANEL);
        unsigned long used = mem_total > mem_available ? mem_total - mem_available : 0;
        snprintf(line, sizeof(line), "Memory   %lu / %lu MiB", used / 1024, mem_total / 1024);
        text(content + 20, 220, line, scale, INK);
        box(content + 20, 268, available - 40, 12, LINE);
        if (mem_total) box(content + 20, 268, (int)((available - 40) * used / mem_total), 12, ACCENT);
        snprintf(line, sizeof(line), "Uptime    %ldh %ldm %lds", uptime_seconds / 3600,
                 uptime_seconds / 60 % 60, uptime_seconds % 60);
        text(content, 350, line, scale, INK);
        snprintf(line, sizeof(line), "Display   %d x %d / %u bit", width, height, var.bits_per_pixel);
        text(content, 385, line, scale, MUTED);
        text(content, 420, "Storage   RAM filesystem (not persistent)", 1, MUTED);
    } else if (page == HELP) {
        text(content, 106, "Getting around", scale + 1, INK);
        const char *lines[] = {
            "Click a card, or use Tab and Enter.",
            "T opens the terminal. Type exit to return.",
            "S shows live memory usage and uptime.",
            "Esc returns here, then exits to the console.",
            "",
            "Terminal commands",
            "  ls       list files       vi       edit a file",
            "  top      processes        python3  Python shell",
            "  ai       local chatbot    mneofetch system info",
            "",
            "Files live in RAM and are lost on shutdown."
        };
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
            text(content, 170 + (int)i * 24, lines[i], 1, i == 5 ? ACCENT : MUTED);
    } else {
        text(content, 106, "End this session?", scale + 1, INK);
        text(content, 168, "Unsaved files in RAM will be lost.", scale, MUTED);
        cards[0] = (struct rect){content, 240, available, 56};
        cards[1] = (struct rect){content, 314, available, 56};
        for (int i = 0; i < 2; i++) {
            box(cards[i].x, cards[i].y, cards[i].w, cards[i].h, PANEL);
            text(content + 20, cards[i].y + 20, i ? "R  Restart" : "Y  Shut down", scale, INK);
        }
        text(content, 402, "Esc cancels", scale, ACCENT);
    }
    cursor();
    present();
}
static void restore(void) {
    if (tty >= 0) {
        if (have_mode) ioctl(tty, KDSETMODE, old_mode);
        if (have_term) tcsetattr(tty, TCSANOW, &saved_term);
    }
}
static void terminal(void) {
    restore();
    const char message[] = "\033[2J\033[HmnOS terminal. Type exit to return to the desktop.\r\n";
    (void)write(tty, message, sizeof(message) - 1);
    pid_t child = fork();
    if (child == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        execl("/bin/sh", "sh", "-l", (char *)NULL);
        _exit(127);
    }
    if (child > 0) {
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            if (!running) kill(child, SIGTERM);
        }
    }
    tcsetpgrp(tty, getpgrp());
    tcsetattr(tty, TCSANOW, &raw_term);
    ioctl(tty, KDSETMODE, KD_GRAPHICS);
}
static void action(int which) {
    if (which == 0) terminal();
    else page = which;
}
static void power(int reboot) {
    restore();
    execl(reboot ? "/sbin/reboot" : "/sbin/poweroff",
          reboot ? "reboot" : "poweroff", "-f", (char *)NULL);
    perror("power action");
    running = 0;
}
static void key(unsigned char c) {
    if (page == POWER) {
        if (c == 'y' || c == 'Y') power(0);
        else if (c == 'r' || c == 'R') power(1);
    }
    if (c == 27 || c == 'q') {
        if (page != HOME) page = HOME;
        else running = 0;
    } else if (c == 't' || c == 'T') terminal();
    else if (c == 's' || c == 'S') page = SYSTEM;
    else if (c == 'h' || c == 'H') page = HELP;
    else if (c == 'p' || c == 'P') page = POWER;
    else if (page == HOME && c == '\t') selected = (selected + 1) % 4;
    else if (page == HOME && (c == '\r' || c == '\n')) action(selected);
}
static void click(void) {
    int left = width >= 900 ? 220 : 156;
    if (mx < left && my >= 130 && my < 130 + 4 * 52) {
        page = (my - 130) / 52;
        return;
    }
    if (page == HOME) {
        for (int i = 0; i < 4; i++) if (inside(cards[i], mx, my)) { action(i); return; }
    } else if (page == POWER) {
        for (int i = 0; i < 2; i++) if (inside(cards[i], mx, my)) { power(i); return; }
    }
}
static int framebuffer_ok(void) {
    unsigned bytes = var.bits_per_pixel / 8;
    if (fix.type != FB_TYPE_PACKED_PIXELS || fix.visual != FB_VISUAL_TRUECOLOR ||
        (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) ||
        var.xres < 640 || var.yres < 480 || var.xres > 4096 || var.yres > 2160 ||
        var.xoffset + (uint64_t)var.xres > var.xres_virtual ||
        var.yoffset + (uint64_t)var.yres > var.yres_virtual ||
        ((uint64_t)var.xoffset + var.xres) * bytes > fix.line_length ||
        ((uint64_t)var.yoffset + var.yres) * fix.line_length > fix.smem_len) return 0;
    struct fb_bitfield fields[] = {var.red, var.green, var.blue, var.transp};
    for (unsigned i = 0; i < 4; i++)
        if (fields[i].length > 8 || fields[i].msb_right ||
            fields[i].offset > var.bits_per_pixel - fields[i].length) return 0;
    return var.red.length && var.green.length && var.blue.length;
}
int main(void) {
    int device = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (device < 0) { perror("desktop: /dev/fb0"); return 1; }
    if (ioctl(device, FBIOGET_VSCREENINFO, &var) || ioctl(device, FBIOGET_FSCREENINFO, &fix) ||
        !framebuffer_ok()) {
        fputs("desktop: need a 640x480 or larger true-color framebuffer\n", stderr);
        close(device); return 1;
    }
    width = (int)var.xres; height = (int)var.yres;
    fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, device, 0);
    canvas = calloc((size_t)width * height, sizeof(*canvas));
    tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fb == MAP_FAILED || !canvas || tty < 0 ||
        tcgetattr(tty, &saved_term) || ioctl(tty, KDGETMODE, &old_mode)) {
        fputs("desktop: run on a Linux virtual console (use start-desktop)\n", stderr);
        return 1;
    }
    have_term = have_mode = 1;
    atexit(restore);
    struct sigaction sa = {0};
    sa.sa_handler = stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL); sigaction(SIGQUIT, &sa, NULL);
    signal(SIGTTOU, SIG_IGN);
    raw_term = saved_term;
    raw_term.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw_term.c_iflag &= ~(IXON | ICRNL);
    raw_term.c_cc[VMIN] = 1; raw_term.c_cc[VTIME] = 0;
    if (tcsetattr(tty, TCSANOW, &raw_term) || ioctl(tty, KDSETMODE, KD_GRAPHICS)) {
        perror("desktop: console setup"); return 1;
    }
    mouse = open("/dev/input/mice", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    mx = width / 2; my = height / 2;
    struct utsname info;
    if (!uname(&info)) snprintf(kernel, sizeof(kernel), "Linux %.64s", info.release);
    struct pollfd fds[] = {{tty, POLLIN, 0}, {mouse, POLLIN, 0}};
    unsigned char packet[3];
    int count = 0, down = 0;
    time_t last = 0, painted = 0;
    int dirty = 1;
    while (running) {
        if (time(NULL) != last) { last = time(NULL); metrics(); }
        if (dirty || page == SYSTEM || last / 60 != painted / 60) {
            draw(); painted = last; dirty = 0;
        }
        int result = poll(fds, 2, 1000);
        if (result < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (fds[0].revents & POLLIN) {
            dirty = 1;
            unsigned char c;
            if (read(tty, &c, 1) == 1) {
                struct pollfd pending = {tty, POLLIN, 0};
                if (c == 27 && poll(&pending, 1, 30) > 0) {
                    unsigned char seq[16];
                    ssize_t n = read(tty, seq, sizeof(seq));
                    if (page == HOME && n >= 2 && seq[0] == '[') {
                        if (seq[1] == 'A' || seq[1] == 'B') selected = (selected + 2) % 4;
                        if (seq[1] == 'C') selected = (selected + 1) % 4;
                        if (seq[1] == 'D') selected = (selected + 3) % 4;
                    }
                } else key(c);
            }
        }
        if (fds[1].revents & POLLIN) {
            dirty = 1;
            unsigned char bytes[96];
            ssize_t n = read(mouse, bytes, sizeof(bytes));
            for (ssize_t i = 0; i < n; i++) {
                if (count == 0 && !(bytes[i] & 8)) continue;
                packet[count++] = bytes[i];
                if (count != 3) continue;
                count = 0;
                if (packet[0] & 0xc0) continue;
                mx += (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
                my -= (int)packet[2] - ((packet[0] & 0x20) ? 256 : 0);
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;
                if (mx >= width) mx = width - 1;
                if (my >= height) my = height - 1;
                int pressed = packet[0] & 1;
                if (pressed && !down) click();
                down = pressed;
            }
        }
    }
    restore();
    have_term = have_mode = 0;
    free(canvas); munmap(fb, fix.smem_len);
    close(device); close(tty);
    if (mouse >= 0) close(mouse);
    return 0;
}
