/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original UEFI handoff: discover firmware tables/GOP, reserve non-conventional
 * memory, ExitBootServices, then run the same freestanding EL1 kernel.
 */
#include "platform.h"
struct guid {
    uint32_t a;
    uint16_t b, c;
    uint8_t d[8];
};
struct header {
    uint64_t signature;
    uint32_t revision, size, crc, reserved;
};
struct console {
    void *reset;
    uint64_t (*print)(void *, const uint16_t *);
};
struct services {
    struct header hdr;
    void *fn[44];
};
struct config {
    struct guid guid;
    void *table;
};
struct system_table {
    struct header hdr;
    void *vendor;
    uint32_t revision;
    void *in_handle, *in, *out_handle;
    struct console *out;
    void *err_handle, *err, *runtime;
    struct services *services;
    size_t count;
    struct config *tables;
};
struct loaded_image {
    uint32_t revision;
    void *parent, *system, *device, *path, *reserved;
    uint32_t options_size;
    uint16_t *options;
    void *base;
    uint64_t size;
    uint32_t code_type, data_type;
    void *unload;
};
struct mode_info {
    uint32_t version, width, height, format, masks[4], pitch;
};
struct gop_mode {
    uint32_t maximum, current;
    struct mode_info *info;
    size_t info_size;
    uint64_t base;
    size_t size;
};
struct gop {
    uint64_t (*query)(void *, uint32_t, size_t *, struct mode_info **);
    uint64_t (*set)(void *, uint32_t);
    void *blt;
    struct gop_mode *mode;
};
struct memory_descriptor {
    uint32_t type, pad;
    uint64_t physical, virtual_address, pages, attributes;
};
static uint8_t memory_map[1u << 20] __attribute__((aligned(4096)));
static size_t map_bytes, map_stride;
static uint8_t entropy[4096];
static size_t entropy_left;
static bool firmware_address(uint64_t address, size_t length) {
    if (!length || address > UINT64_MAX - length)
        return false;
    for (size_t at = 0; at < map_bytes; at += map_stride) {
        const struct memory_descriptor *d = (const void *)(memory_map + at);
        if (d->type != 9 && d->type != 10 && d->type != 4 && d->type != 2 && d->type != 7)
            continue;
        if (d->pages <= UINT64_MAX / 4096 && address >= d->physical &&
            address - d->physical <= d->pages * 4096 &&
            length <= d->pages * 4096 - (address - d->physical))
            return true;
    }
    return false;
}
bool arm_firmware_random(uint8_t *out, size_t n) {
    if (n > entropy_left)
        return false;
    entropy_left -= n;
    memcpy(out, entropy + entropy_left, n);
    memset(entropy + entropy_left, 0, n);
    return true;
}
static const struct guid acpi_guid = {
    0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static const struct guid rng_guid = {
    0x3152bca5, 0xeade, 0x433d, {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}};
static const struct guid fdt_guid = {
    0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};
static const struct guid loaded_guid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct guid gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
static uint64_t message(struct system_table *s, const uint16_t *m) {
    s->out->print(s->out, m);
    return 0x8000000000000003ULL;
}
static void framebuffer(struct services *bs) {
    uint64_t (*locate)(const struct guid *, void *, void **) = (void *)bs->fn[37];
    struct gop *g = NULL;
    if (locate(&gop_guid, NULL, (void **)&g) || !g || !g->mode)
        return;
    unsigned best=g->mode->current,best_score=~0u;
    for(uint32_t i=0;i<g->mode->maximum;i++) {
        size_t bytes=0;struct mode_info *m=NULL;
        if(!g->query(g,i,&bytes,&m)&&m) {
            if(m->format<=1&&m->width>=1024&&m->width<=1920&&m->height>=720&&m->height<=1080) {
                unsigned score=(m->width>1280?m->width-1280:1280-m->width)+(m->height>720?m->height-720:720-m->height);
                if(score<best_score){best=i;best_score=score;}
            }
            uint64_t (*free_pool)(void *)=(void *)bs->fn[6];free_pool(m);
        }
    }
    if(best!=g->mode->current)g->set(g,best);
    struct mode_info *m = g->mode->info;
    if (!m || m->width < 640 || m->height < 480 || m->width > 2560 || m->height > 1440 ||
        m->format > 1 || m->pitch < m->width || m->pitch > 16384)
        return;
    if ((uint64_t)m->pitch * m->height * 4 > g->mode->size)
        return;
    boot.fb = (struct framebuffer){(void *)(uintptr_t)g->mode->base,
                                   m->width,
                                   m->height,
                                   m->pitch * 4,
                                   m->format == 0 ? 0 : 16,
                                   8,
                                   m->format == 0 ? 16 : 0};
}
static bool image_layout(struct loaded_image *image) {
    uint8_t *base = image->base;
    uint32_t pe = *(uint32_t *)(base + 0x3c);
    if (image->size < 4096 || pe > image->size - 256 || memcmp(base + pe, "PE\0\0", 4))
        return false;
    uint16_t count = *(uint16_t *)(base + pe + 6), optional = *(uint16_t *)(base + pe + 20);
    size_t at = pe + 24 + optional;
    if (at > image->size || (size_t)count * 40 > image->size - at)
        return false;
    platform.image_start = (uintptr_t)base;
    platform.image_end = (uintptr_t)base + image->size;
    for (unsigned i = 0; i < count; ++i) {
        uint8_t *s = base + at + i * 40;
        uint32_t size = *(uint32_t *)(s + 8), rva = *(uint32_t *)(s + 12),
                 flags = *(uint32_t *)(s + 36);
        if (rva > image->size || size > image->size - rva)
            return false;
        if (flags & 0x20000000) {
            if (platform.text_start || (flags & 0x80000000))
                return false;
            platform.text_start = (uintptr_t)base + rva;
            platform.text_end = (platform.text_start + size + 4095) & ~4095ULL;
        }
        if (!memcmp(s, ".rdata", 6)) {
            platform.ro_start = (uintptr_t)base + rva;
            platform.ro_end = (platform.ro_start + size + 4095) & ~4095ULL;
        }
    }
    return platform.text_start != 0;
}
uint64_t efi_main(void *image_handle, struct system_table *s) {
    if (!s || !s->services)
        return 0x8000000000000002ULL;
    struct services *bs = s->services;
    uint64_t (*watchdog)(size_t, uint64_t, size_t, uint16_t *) = (void *)bs->fn[29];
    watchdog(0, 0, 0, NULL);
    uint64_t (*get_map)(size_t *, void *, size_t *, size_t *, uint32_t *) = (void *)bs->fn[4];
    uint64_t (*exit_boot)(void *, size_t) = (void *)bs->fn[26];
    size_t initial_key;
    uint32_t initial_version;
    map_bytes = sizeof memory_map;
    if (get_map(&map_bytes, memory_map, &initial_key, &map_stride, &initial_version) ||
        map_stride < sizeof(struct memory_descriptor) || map_bytes % map_stride)
        return 0x8000000000000009ULL;
    const void *dt = NULL, *rsdp = NULL;
    for (size_t i = 0; i < s->count; ++i) {
        if (!memcmp(&s->tables[i].guid, &fdt_guid, sizeof fdt_guid))
            dt = s->tables[i].table;
        if (!memcmp(&s->tables[i].guid, &acpi_guid, sizeof acpi_guid))
            rsdp = s->tables[i].table;
    }
    if (dt && !rsdp) {
        if (!arm_fdt(dt))
            return message(s, (const uint16_t *)L"mnOS: invalid device tree.\r\n");
    } else {
        if (!rsdp || !arm_acpi(rsdp, firmware_address))
            return message(s, (const uint16_t *)L"mnOS: unsupported ACPI platform (requires "
                                                L"PL011/SBSA UART, GICv2/v3 and PSCI).\r\n");
        for (size_t at = 0; at < map_bytes; at += map_stride) {
            struct memory_descriptor *d = (void *)(memory_map + at);
            if ((d->type >= 1 && d->type <= 7) || d->type == 9 || d->type == 10) {
                uint64_t bytes = d->pages * 4096;
                if (d->pages > UINT64_MAX / 4096 || d->physical > UINT64_MAX - bytes)
                    return 0x8000000000000002ULL;
                if (!bytes)
                    continue;
                unsigned r = platform.ram_count;
                if (r && platform.ram[r - 1].start + platform.ram[r - 1].size == d->physical)
                    platform.ram[r - 1].size += bytes;
                else {
                    if (r == ARM_RANGES)
                        return 0x8000000000000009ULL;
                    platform.ram[r] = (struct arm_range){d->physical, bytes};
                    ++platform.ram_count;
                }
            }
        }
    }
    struct rng_protocol {
        void *info;
        uint64_t (*get)(void *, const struct guid *, size_t, uint8_t *);
    } *rng = NULL;
    uint64_t (*locate)(const struct guid *, void *, void **) = (void *)bs->fn[37];
    if (!locate(&rng_guid, NULL, (void **)&rng) && rng &&
        !rng->get(rng, NULL, sizeof entropy, entropy))
        entropy_left = sizeof entropy;
    struct loaded_image *loaded = NULL;
    uint64_t (*handle)(void *, const struct guid *, void **) = (void *)bs->fn[16];
    if (handle(image_handle, &loaded_guid, (void **)&loaded) || !loaded || !image_layout(loaded))
        return message(s, (const uint16_t *)L"mnOS: invalid loaded image.\r\n");
    if (loaded->options && loaded->options_size >= 30) {
        for (size_t i = 0; i + 15 <= loaded->options_size / 2; ++i) {
            bool match = true;
            const char *arg = "mnos.selftest=1";
            for (unsigned k = 0; k < 15; ++k)
                if (loaded->options[i + k] != (uint8_t)arg[k])
                    match = false;
            if (match)
                boot.selftest = true;
        }
    }
    framebuffer(bs);
    platform.uefi = true;
    s->out->print(
        s->out,
        (const uint16_t
             *)L"mnOS: leaving UEFI boot services; starting the original ARM64 kernel.\r\n");
    unsigned reserved = platform.reserved_count;
    for (unsigned retry = 0; retry < 4; ++retry) {
        size_t bytes = sizeof memory_map, key = 0, stride = 0;
        uint32_t version = 0;
        if (get_map(&bytes, memory_map, &key, &stride, &version) ||
            stride < sizeof(struct memory_descriptor) || bytes % stride)
            return 0x8000000000000009ULL;
        platform.reserved_count = reserved;
        for (size_t at = 0; at < bytes; at += stride) {
            struct memory_descriptor *d = (void *)(memory_map + at);
            if (d->type == 7 || !d->pages)
                continue;
            /* Only intersect physical RAM: MMIO descriptors are not allocator input. */
            for (unsigned r = 0; r < platform.ram_count; ++r) {
                uint64_t lo = platform.ram[r].start, hi = lo + platform.ram[r].size;
                if (d->pages > UINT64_MAX / 4096 || d->physical > UINT64_MAX - d->pages * 4096)
                    return 0x8000000000000002ULL;
                uint64_t end = d->physical + d->pages * 4096;
                if (lo < d->physical)
                    lo = d->physical;
                if (hi > end)
                    hi = end;
                if (lo < hi) {
                    if (platform.reserved_count == ARM_RANGES)
                        return 0x8000000000000009ULL;
                    platform.reserved[platform.reserved_count++] = (struct arm_range){lo, hi - lo};
                }
            }
        }
        if (!exit_boot(image_handle, key)) {
            extern void arm_firmware_entry(void);
            arm_firmware_entry();
            __builtin_unreachable();
        }
    }
    return 0x8000000000000002ULL;
}
/* Same kernel and handoff; test artifact differs only in its PE entry point. */
uint64_t efi_test_main(void *handle, struct system_table *s) {
    boot.selftest = true;
    return efi_main(handle, s);
}
