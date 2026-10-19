/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original graphical UEFI installer. Firmware services are used only while
 * installing; the installed image exits boot services into the mnOS kernel.
 * No Linux, shell, Python, network or external installer runs in this image. */
#include "disk_layout.h"
#include "efi.h"
#include "mnfs.h"
#include "font.h"
extern const uint8_t system_payload[], system_payload_end[];
struct media {
    uint32_t id;
    uint8_t removable, present, logical, readonly, caching;
    uint32_t blocksize, align;
    uint64_t last;
};
struct block {
    uint64_t revision;
    struct media *media;
    void *reset;
    status (*read)(void *, uint32_t, uint64_t, size_t, void *);
    status (*write)(void *, uint32_t, uint64_t, size_t, const void *);
    status (*flush)(void *);
};
struct path_node {
    uint8_t type, sub;
    uint16_t size;
};
struct target {
    struct block *block;
    void *handle;
};
static const struct guid block_guid = {
    0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct guid path_guid = {
    0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct guid rng_guid = {
    0x3152bca5, 0xeade, 0x433d, {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}};
static struct system_table *system;
static struct services *bs;
static void *self;
static struct target targets[16];
static unsigned count, chosen, stage, percent;
static struct gop *video;
static uint32_t *pixels;
static unsigned width, height, pitch, format;
static char confirm[6], message[100];
static unsigned used;
static uint8_t bounce[65536] __attribute__((aligned(4096)));
static bool failed;
struct pointer_state {
    int32_t x, y, z;
    uint8_t left, right;
};
struct pointer {
    void *reset;
    status (*state)(void *, struct pointer_state *);
    void *event, *mode;
};
struct absolute_state {
    uint64_t x, y, z;
    uint32_t buttons;
};
struct absolute_mode {
    uint64_t minx, miny, minz, maxx, maxy, maxz;
    uint32_t attributes;
};
struct absolute {
    void *reset;
    status (*state)(void *, struct absolute_state *);
    void *event;
    struct absolute_mode *mode;
};
static struct pointer *mouse;
static struct absolute *tablet;
static int mouse_x = 400, mouse_y = 300;
static bool mouse_down, mouse_visible;
static uint64_t data_blocks;
static void decimal(char *p, uint64_t n) {
    char b[24];
    unsigned k = 0;
    do {
        b[k++] = '0' + n % 10;
        n /= 10;
    } while (n);
    unsigned i = 0;
    while (k)
        p[i++] = b[--k];
    p[i] = 0;
}
static void log_message(const char *s) {
    uint16_t out[256];
    unsigned i = 0;
    while (s[i] && i < 252) {
        out[i] = (uint8_t)s[i];
        i++;
    }
    out[i++] = '\r';
    out[i++] = '\n';
    out[i] = 0;
    system->out->print(system->out, out);
}
static uint32_t rgb(uint32_t c) {
    return format ? c : ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255);
}
static void rect(int x, int y, int w, int h, uint32_t c) {
    if (!pixels)
        return;
    for (int j = y; j < y + h && j < (int)height; j++)
        if (j >= 0)
            for (int i = x; i < x + w && i < (int)width; i++)
                if (i >= 0)
                    pixels[(size_t)j * pitch + i] = rgb(c);
}
static void text(int x, int y, const char *s, unsigned size, uint32_t c) {
    if (!pixels)
        return;
    size_t p = 0, length = strlen(s);
    int advance = 0;
    while (p < length && x + advance / 64 < (int)width - 20) {
        const struct glyph *g = font_find(font_decode(s, length, &p), size);
        if (!g)
            continue;
        for (unsigned j = 0; j < g->height; j++)
            for (unsigned i = 0; i < g->width; i++) {
                int px = x + (advance + 32) / 64 + g->left + (int)i,
                    py = y + g->top + (int)j;
                unsigned a = font_pixels[g->offset + j * g->width + i];
                if (a && px >= 0 && py >= 0 && px < (int)width && py < (int)height) {
                    uint32_t bg = rgb(pixels[(size_t)py * pitch + px]);
                    unsigned r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255,
                             gc = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255,
                             b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
                    pixels[(size_t)py * pitch + px] = rgb((r << 16) | (gc << 8) | b);
                }
            }
        advance += g->advance;
    }
}
static void draw(void) {
    rect(0, 0, width, height, 0x101d29);
    int x = (int)width / 2 - 380, y = (int)height / 2 - 290;
    text(x, y, "mnOS / INSTALL", 18, 0x83d5bc);
    text(x, y + 43, "Your own native desktop.", 28, 0xe2ebf0);
#ifdef __aarch64__
    text(x, y + 99, "ARM64  /  UEFI  /  Original kernel + desktop", 18, 0x9aacba);
#else
    text(x, y + 99, "AMD64  /  UEFI  /  Original kernel + desktop", 18, 0x9aacba);
#endif
    if (stage == 0) {
        text(x, y + 155, "Choose the target disk", 18, 0xe2ebf0);
        for (unsigned i = 0; i < count && i < 5; i++) {
            int yy = y + 198 + (int)i * 46;
            rect(x, yy, 760, 40, i == chosen ? 0x31544d : 0x1f303e);
            char line[100] = "Disk ";
            decimal(line + 5, i + 1);
            size_t n = strlen(line);
            memcpy(line + n, "   /   ", 8);
            decimal(line + n + 7, (targets[i].block->media->last + 1) / 2048);
            n = strlen(line);
            memcpy(line + n, " MiB   /   512-byte sectors", 27);
            text(x + 12, yy + 3, line, 18, 0xe2ebf0);
        }
        if (!count)
            text(x, y + 210, "No supported writable disk found (minimum 80 MiB).", 18, 0xe9bb73);
        text(x, y + 453, "Up / Down select    Enter install    B try without installing", 18,
             0x83d5bc);
        text(x, y + 495, "A confirmation is required before any disk is changed.", 18, 0x9aacba);
    } else if (stage == 1) {
        text(x, y + 165, "Erase the selected disk and install mnOS?", 28, 0xe9bb73);
        char label[80] = "Selected disk ";
        decimal(label + 14, chosen + 1);
        text(x, y + 222, label, 18, 0xe2ebf0);
        text(x, y + 265, "All data on this disk will be removed. Other disks stay untouched.", 18,
             0xe2ebf0);
        rect(x, y + 317, 760, 54, 0x1f303e);
        text(x + 15, y + 326, used ? confirm : "Type ERASE to confirm", 18, 0x83d5bc);
        text(x, y + 402, "Enter confirm    Esc go back", 18, 0x9aacba);
    } else if (stage == 2) {
        text(x, y + 185, "Installing native mnOS", 28, 0xe2ebf0);
        text(x, y + 248, "Writing boot files and creating the persistent file system...", 18,
             0x9aacba);
        rect(x, y + 320, 760, 8, 0x2b4050);
        rect(x, y + 320, 760 * (int)percent / 100, 8, 0x83d5bc);
        char number[32];
        decimal(number, percent);
        text(x, y + 350, number, 28, 0xe2ebf0);
    } else {
        text(x, y + 185, failed ? "Installation stopped" : "Installation complete", 28,
             failed ? 0xefb4ab : 0x83d5bc);
        text(x, y + 247, message, 18, 0xe2ebf0);
        if (!failed) {
            text(x, y + 302, "Eject this ISO, then restart from the installed disk.", 18, 0xe2ebf0);
            text(x, y + 348, "Create your account on first boot. Your files stay on disk.", 18,
                 0x9aacba);
        }
        text(x, y + 422, "R restart    Esc return to disk selection", 18, 0x83d5bc);
    }
    if (stage == 0) {
        rect(x, y + 402, 230, 36, 0x31544d);
        text(x + 15, y + 404, "Continue to install", 18, 0xe2ebf0);
    }
    text(x, y + 558, "mnOS Desktop 0.4   /   Native applications, no Linux runtime", 18, 0x617f91);
    if (mouse_visible)
        for (int n = 0; n < 15; n++)
            rect(mouse_x, mouse_y + n, n / 2 + 1, 1, 0xe2ebf0);
}
static void *allocate(size_t bytes) {
    void *p = NULL;
    status (*fn)(uint32_t, size_t, void **) = (void *)bs->fn[5];
    return fn(2, bytes, &p) ? NULL : p;
}
static void release(void *p) {
    if (p) {
        status (*fn)(void *) = (void *)bs->fn[6];
        fn(p);
    }
}
static bool raw_read(void *context, uint64_t lba, unsigned n, void *p) {
    struct block *b = context;
    if (!n || n > 128 || lba > b->media->last || n - 1 > b->media->last - lba)
        return false;
    if (b->read(b, b->media->id, lba, (size_t)n * 512, bounce))
        return false;
    memcpy(p, bounce, (size_t)n * 512);
    return true;
}
static bool raw_write(void *context, uint64_t lba, unsigned n, const void *p) {
    struct block *b = context;
    if (!n || n > 128 || lba > b->media->last || n - 1 > b->media->last - lba)
        return false;
    memcpy(bounce, p, (size_t)n * 512);
    return !b->write(b, b->media->id, lba, (size_t)n * 512, bounce);
}
static bool flush(void *p) {
    struct block *b = p;
    return !b->flush(b);
}
static bool fs_read(void *p, uint64_t block, void *bytes) {
    return block < data_blocks && raw_read(p, MN_DATA_START + block * 8, 8, bytes);
}
static bool fs_write(void *p, uint64_t block, const void *bytes) {
    return block < data_blocks && raw_write(p, MN_DATA_START + block * 8, 8, bytes);
}
static void progress(unsigned n) {
    percent = n;
    draw();
}
static bool source_disk(void *source, void *target) {
    status (*handle)(void *, const struct guid *, void **) = (void *)bs->fn[16];
    struct path_node *a = NULL, *b = NULL;
    if (handle(source, &path_guid, (void **)&a) || handle(target, &path_guid, (void **)&b))
        return true;
    size_t n = 0;
    while (b->type != 0x7f) {
        if (b->size < 4 || b->size > 1024 || n + b->size > 4096)
            return true;
        n += b->size;
        b = (void *)((uint8_t *)b + b->size);
    }
    size_t m = 0;
    while (a->type != 0x7f) {
        if (a->size < 4 || a->size > 1024 || m + a->size > 4096)
            return true;
        m += a->size;
        a = (void *)((uint8_t *)a + a->size);
    }
    void *ap = NULL, *bp = NULL;
    handle(source, &path_guid, &ap);
    handle(target, &path_guid, &bp);
    return m >= n && !memcmp(ap, bp, n);
}
static bool native_target(void *device) {
    /* Firmware may expose storage for which the native kernel has no driver.
     * Refuse installation there instead of creating an unbootable system. */
    status (*handle)(void *, const struct guid *, void **) = (void *)bs->fn[16];
    struct path_node *p = NULL;
    if (handle(device, &path_guid, (void **)&p) || !p)
        return false;
    size_t bytes = 0;
    bool supported = false;
    while (p->type != 0x7f) {
        if (p->size < 4 || p->size > 1024 || bytes + p->size > 4096)
            return false;
#ifdef __aarch64__
        const struct guid mmio = {
            0x837dca9e, 0xe874, 0x4d82, {0xb2, 0x9a, 0x23, 0xfe, 0x0e, 0x23, 0xd1, 0xe2}};
        if (p->type == 1 && p->sub == 4 && p->size >= 20 && !memcmp((uint8_t *)p + 4, &mmio, 16))
            supported = true;
#else
        if (p->type == 3 && p->sub == 1 && p->size >= 8)
            supported = true;
#endif
        bytes += p->size;
        p = (void *)((uint8_t *)p + p->size);
    }
    return supported;
}
static void discover(void) {
    status (*handles)(uint32_t, const struct guid *, void *, size_t *, void ***) =
        (void *)bs->fn[36];
    status (*handle)(void *, const struct guid *, void **) = (void *)bs->fn[16];
    struct loaded_image *loaded = NULL;
    count = chosen = 0;
    if (handle(self, &loaded_guid, (void **)&loaded) || !loaded)
        return;
    size_t n = 0;
    void **list = NULL;
    if (handles(2, &block_guid, NULL, &n, &list))
        return;
    for (size_t i = 0; i < n && count < 16; i++) {
        struct block *b = NULL;
        if (handle(list[i], &block_guid, (void **)&b) || !b || !b->media)
            continue;
        struct media *m = b->media;
        if (!m->present || m->readonly || m->logical || m->blocksize != 512 || m->align > 4096 ||
            m->last < MN_DATA_START + 32768 || source_disk(loaded->device, list[i]) ||
            !native_target(list[i]))
            continue;
        targets[count++] = (struct target){b, list[i]};
    }
    release(list);
    log_message(count ? "INSTALLER READY: choose disk; Enter; type ERASE; Enter."
                      : "INSTALLER: no writable supported target disk.");
}
static void install(void) {
    stage = 2;
    percent = 0;
    draw();
    struct block *b = targets[chosen].block;
    uint8_t uuid[16];
    struct rng {
        void *info;
        status (*get)(void *, const struct guid *, size_t, uint8_t *);
    } *rng = NULL;
    status (*locate)(const struct guid *, void *, void **) = (void *)bs->fn[37];
    bool ok =
        !locate(&rng_guid, NULL, (void **)&rng) && rng && !rng->get(rng, NULL, sizeof uuid, uuid);
    /* GPT IDs are identifiers, not password entropy. Fall back to a per-disk
     * deterministic ID when firmware lacks RNG; account creation still requires
     * a cryptographic RNG inside the kernel. */
    if (!ok) {
        memcpy(uuid, "MNOS-INSTALL-001", 16);
        memcpy(uuid + 8, &b->media->last, 8);
    }
    uuid[7] = (uuid[7] & 15) | 0x40;
    uuid[8] = (uuid[8] & 63) | 0x80;
    struct disk_io raw = {b, b->media->last + 1, raw_read, raw_write, flush};
#ifdef __aarch64__
    const bool arm64 = true;
#else
    const bool arm64 = false;
#endif
    ok = disk_install_boot(&raw, system_payload, (size_t)(system_payload_end - system_payload),
                           arm64, uuid, progress);
    struct mnfs fs = {0};
    data_blocks = (b->media->last + 1 - 34 - MN_DATA_START) / 8;
    struct mnfs_io io = {b, data_blocks, fs_read, fs_write, flush, allocate, release};
    if (ok)
        ok = mnfs_format(&fs, &io) == MN_OK;
    if (ok)
        ok = !mnfs_create(&fs, "/system", true) && !mnfs_create(&fs, "/home", true) &&
             !mnfs_create(&fs, "/home/readme.txt", false);
    const char *welcome = "Welcome to mnOS Desktop.\nThis disk boots the original mnOS kernel and "
                          "native desktop.\nFiles, editor, terminal, settings and local accounts "
                          "are integrated.\nYour personal files are in /disk/home/USERNAME.\n";
    if (ok)
        ok = mnfs_write(&fs, "/home/readme.txt", 0, welcome, strlen(welcome)) == MN_OK;
    if (fs.used)
        mnfs_close(&fs);
    uint64_t start = 0, length = 0;
    if (ok)
        ok = flush(b) && disk_data_partition(&raw, &start, &length) && start == MN_DATA_START;
    /* Verify every installed boot byte, not just a successful write return. */
    uint8_t verify[8192];
    size_t payload_size = system_payload_end - system_payload;
    for (size_t at = 0; ok && at < payload_size; at += sizeof verify) {
        size_t len = payload_size - at;
        if (len > sizeof verify)
            len = sizeof verify;
        ok = raw_read(b, MN_ESP_START + 32 + 2048 + 3 + at / 512, (unsigned)((len + 511) / 512),
                      verify) &&
             !memcmp(verify, system_payload + at, len);
    }
    if (ok) {
        struct mnfs check = {0};
        ok = mnfs_mount(&check, &io, false) == MN_OK;
        if (ok)
            mnfs_close(&check);
    }
    failed = !ok;
    const char *result = ok ? "Boot files and persistent storage verified."
                            : "Disk write or verification failed. Do not use this installation.";
    memcpy(message, result, strlen(result) + 1);
    percent = 100;
    stage = 3;
    log_message(ok ? "INSTALL PASS: GPT, FAT32 boot payload readback and MNFS mount verified."
                   : "INSTALL FAIL: disk write/readback.");
    draw();
}
static void start_desktop(void) {
    status (*load)(uint8_t, void *, void *, void *, size_t, void **) = (void *)bs->fn[22];
    status (*start)(void *, size_t *, uint16_t **) = (void *)bs->fn[23];
    void *image = NULL;
    if (load(0, self, NULL, (void *)system_payload, system_payload_end - system_payload, &image) ||
        start(image, NULL, NULL)) {
        failed = true;
        stage = 3;
        memcpy(message, "Native boot failed. Check firmware and device compatibility.",
               sizeof "Native boot failed. Check firmware and device compatibility.");
        draw();
    }
}
static void restart(void) {
    struct runtime {
        struct header hdr;
        void *fn[14];
    };
    struct runtime *rt = system->runtime;
    void (*reset)(uint32_t, status, size_t, void *) = (void *)rt->fn[10];
    reset(0, 0, 0, NULL);
}
status efi_main(void *image, struct system_table *s) {
    self = image;
    system = s;
    bs = s->services;
    status (*watchdog)(size_t, uint64_t, size_t, uint16_t *) = (void *)bs->fn[29];
    watchdog(0, 0, 0, NULL);
    status (*locate)(const struct guid *, void *, void **) = (void *)bs->fn[37];
    if (!locate(&gop_guid, NULL, (void **)&video) && video && video->mode) {
        unsigned best = video->mode->current, score_best = ~0u;
        for (uint32_t i = 0; i < video->mode->maximum; i++) {
            struct mode_info *m = NULL;
            size_t n = 0;
            if (!video->query(video, i, &n, &m) && m) {
                if (m->format < 2 && m->width >= 1024 && m->width <= 1920 && m->height >= 720 &&
                    m->height <= 1080) {
                    unsigned score = (m->width > 1280 ? m->width - 1280 : 1280 - m->width) +
                                     (m->height > 720 ? m->height - 720 : 720 - m->height);
                    if (score < score_best) {
                        best = i;
                        score_best = score;
                    }
                }
                release(m);
            }
        }
        if (best != video->mode->current)
            video->set(video, best);
        struct mode_info *m = video->mode->info;
        if (m && m->format < 2 && m->width >= 800 && m->height >= 600 && m->pitch >= m->width &&
            m->pitch <= 16384 && (uint64_t)m->pitch * m->height * 4 <= video->mode->size) {
            pixels = (void *)(uintptr_t)video->mode->base;
            width = m->width;
            height = m->height;
            pitch = m->pitch;
            format = m->format;
        }
    }
    const struct guid pointer_guid = {
        0x31878c87, 0x0b75, 0x11d5, {0x9a, 0x4f, 0, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
    const struct guid absolute_guid = {
        0x8d59d32b, 0xc655, 0x4ae9, {0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43}};
    locate(&pointer_guid, NULL, (void **)&mouse);
    locate(&absolute_guid, NULL, (void **)&tablet);
    mouse_x = width / 2;
    mouse_y = height / 2;
    discover();
    draw();
    status (*stall)(size_t) = (void *)bs->fn[28];
    for (;;) {
        struct key k = {0};
        bool key_ready = !s->in->read(s->in, &k), changed = false, down = mouse_down;
        struct pointer_state relative = {0};
        struct absolute_state absolute = {0};
        if (mouse && !mouse->state(mouse, &relative)) {
            mouse_x += relative.x;
            mouse_y += relative.y;
            down = relative.left != 0;
            changed = true;
        }
        if (tablet && tablet->mode && !tablet->state(tablet, &absolute)) {
            struct absolute_mode *m = tablet->mode;
            if (m->maxx > m->minx && m->maxy > m->miny && absolute.x >= m->minx &&
                absolute.x <= m->maxx && absolute.y >= m->miny && absolute.y <= m->maxy) {
                uint64_t dx = absolute.x - m->minx, dy = absolute.y - m->miny;
                if (dx <= UINT64_MAX / (width ? width : 1) &&
                    dy <= UINT64_MAX / (height ? height : 1)) {
                    mouse_x = (int)(dx * width / (m->maxx - m->minx));
                    mouse_y = (int)(dy * height / (m->maxy - m->miny));
                    down = (absolute.buttons & 1) != 0;
                    changed = true;
                }
            }
        }
        if (changed) {
            if (mouse_x < 0)
                mouse_x = 0;
            if (mouse_y < 0)
                mouse_y = 0;
            if (mouse_x >= (int)width)
                mouse_x = (int)width - 1;
            if (mouse_y >= (int)height)
                mouse_y = (int)height - 1;
            mouse_visible = true;
            int x = (int)width / 2 - 380, y = (int)height / 2 - 290;
            if (down && !mouse_down && stage == 0 && mouse_x >= x && mouse_x < x + 760) {
                if (mouse_y >= y + 198 && mouse_y < y + 198 + 46 * (int)(count < 5 ? count : 5))
                    chosen = (unsigned)(mouse_y - y - 198) / 46;
                if (mouse_x < x + 230 && mouse_y >= y + 402 && mouse_y < y + 438 && count) {
                    k.unicode = '\r';
                    key_ready = true;
                }
            }
            mouse_down = down;
        }
        if (!key_ready) {
            if (changed)
                draw();
            stall(10000);
            continue;
        }
        char c = k.unicode < 128 ? (char)k.unicode : 0;
        if (stage == 0) {
            if (k.scan == 1 && chosen)
                chosen--;
            else if (k.scan == 2 && chosen + 1 < count)
                chosen++;
            else if (c == 'b' || c == 'B')
                start_desktop();
            else if (c == '\r' && count) {
                stage = 1;
                used = 0;
                confirm[0] = 0;
                log_message("INSTALL CONFIRM: type ERASE then Enter. Esc cancels without writing.");
            }
        } else if (stage == 1) {
            if (k.scan == 23 || c == 27) {
                stage = 0;
                used = 0;
                log_message("INSTALL CANCELLED: no disk writes.");
            } else if (c == '\b') {
                if (used)
                    confirm[--used] = 0;
            } else if (c == '\r' && used == 5 && !memcmp(confirm, "ERASE", 5))
                install();
            else if (c >= 32 && c < 127 && used < 5) {
                confirm[used++] = c;
                confirm[used] = 0;
            }
        } else if (stage == 3) {
            if (c == 'r' || c == 'R')
                restart();
            else if (k.scan == 23 || c == 27) {
                stage = 0;
                discover();
            }
        }
        draw();
    }
}
