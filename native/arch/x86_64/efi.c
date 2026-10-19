/* SPDX-License-Identifier: GPL-3.0-or-later
 * mnOS original x86-64 EFI loader. Loads our native ELF, builds a bounded boot
 * map, leaves firmware services, enters our 64-bit kernel trampoline. */
#include "../../installer/efi.h"
extern const uint8_t kernel_payload[], kernel_payload_end[];
struct elf {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phsize, phnum, shsize, shnum, shstr;
};
struct segment {
    uint32_t type, flags;
    uint64_t offset, address, physical, size, memory, align;
};
static uint8_t map[1 << 20];
static status fail(struct system_table *s, const uint16_t *m) {
    s->out->print(s->out, m);
    return ERROR | 3;
}
status efi_main(void *handle, struct system_table *s) {
    struct services *bs = s->services;
    status (*pages)(uint32_t, uint32_t, size_t, uint64_t *) = (void *)bs->fn[2];
    status (*get_map)(size_t *, void *, size_t *, size_t *, uint32_t *) = (void *)bs->fn[4];
    status (*exit_boot)(void *, size_t) = (void *)bs->fn[26];
    status (*locate)(const struct guid *, void *, void **) = (void *)bs->fn[37];
    status (*free_pool)(void *) = (void *)bs->fn[6];
    status (*watchdog)(size_t, uint64_t, size_t, uint16_t *) = (void *)bs->fn[29];
    watchdog(0, 0, 0, NULL);
    const struct elf *e = (const void *)kernel_payload;
    size_t len = kernel_payload_end - kernel_payload;
    if (len < sizeof *e || memcmp(e->ident, "\177ELF\2\1\1", 7) || e->machine != 62 ||
        e->phsize != sizeof(struct segment) || e->phnum > 16 || e->phoff > len ||
        e->phnum * sizeof(struct segment) > len - e->phoff)
        return fail(s, (void *)L"Invalid native ELF.\r\n");
    uint64_t high = 0;
    bool entry = false;
    for (unsigned i = 0; i < e->phnum; i++) {
        const struct segment *p = (const void *)(kernel_payload + e->phoff + i * e->phsize);
        if (p->type != 1)
            continue;
        if (p->physical != p->address || p->address < 0x2000000 || p->address >= 0x4000000 ||
            p->memory > 0x4000000 - p->address || p->size > p->memory || p->offset > len ||
            p->size > len - p->offset)
            return fail(s, (void *)L"Invalid native segment.\r\n");
        if (p->address + p->memory > high)
            high = p->address + p->memory;
        if ((p->flags & 1) && e->entry >= p->address && e->entry - p->address < p->size)
            entry = true;
    }
    uint64_t base = 0x2000000;
    if (!entry || high <= base || pages(2, 2, (high - base + 4095) / 4096, &base))
        return fail(s, (void *)L"Native low-memory reservation unavailable.\r\n");
    memset((void *)(uintptr_t)base, 0, high - base);
    for (unsigned i = 0; i < e->phnum; i++) {
        const struct segment *p = (const void *)(kernel_payload + e->phoff + i * e->phsize);
        if (p->type == 1)
            memcpy((void *)(uintptr_t)p->address, kernel_payload + p->offset, p->size);
    }
    uint64_t address = 0xffffffff;
    if (pages(1, 2, 256, &address))
        return fail(s, (void *)L"No memory for boot map.\r\n");
    uint8_t *mb = (void *)(uintptr_t)address;
    memset(mb, 0, 1 << 20);
    size_t prefix = 8;
    struct gop *g = NULL;
    if (!locate(&gop_guid, NULL, (void **)&g) && g && g->mode) {
        for (uint32_t i = 0; i < g->mode->maximum; i++) {
            struct mode_info *m = NULL;
            size_t n = 0;
            if (!g->query(g, i, &n, &m) && m) {
                bool choose = m->width == 1280 && m->height == 720;
                free_pool(m);
                if (choose) {
                    g->set(g, i);
                    break;
                }
            }
        }
        struct mode_info *m = g->mode->info;
        if (m && m->format <= 1 && m->width <= 2560 && m->height <= 1440 &&
            g->mode->base < 0x100000000ULL) {
            struct fb {
                uint32_t type, size;
                uint64_t base;
                uint32_t pitch, w, h;
                uint8_t bpp, kind;
                uint16_t reserved;
                uint8_t rp, rs, gp, gs, bp, bs;
            } __attribute__((packed)) f = {8,
                                           38,
                                           g->mode->base,
                                           m->pitch * 4,
                                           m->width,
                                           m->height,
                                           32,
                                           1,
                                           0,
                                           m->format ? 16 : 0,
                                           8,
                                           8,
                                           8,
                                           m->format ? 0 : 16,
                                           8};
            memcpy(mb + prefix, &f, sizeof f);
            prefix += 40;
        }
    }
    const struct guid acpi = {
        0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
    for (size_t i = 0; i < s->count; i++)
        if (!memcmp(&s->tables[i].guid, &acpi, 16)) {
            uint32_t h[2] = {15, 44};
            memcpy(mb + prefix, h, 8);
            memcpy(mb + prefix + 8, s->tables[i].table, 36);
            prefix += 48;
            break;
        }
    s->out->print(s->out, (void *)L"mnOS: starting original x86-64 graphical system.\r\n");
    for (unsigned retry = 0; retry < 4; retry++) {
        size_t bytes = sizeof map, key = 0, stride = 0;
        uint32_t version = 0;
        if (get_map(&bytes, map, &key, &stride, &version) ||
            stride < sizeof(struct memory_descriptor) || bytes % stride)
            return ERROR | 9;
        size_t at = prefix;
        uint32_t tag[4] = {6, 16, 24, 0};
        memcpy(mb + at, tag, 16);
        at += 16;
        for (size_t k = 0; k < bytes; k += stride) {
            struct memory_descriptor *d = (void *)(map + k);
            if (!d->pages)
                continue;
            if (at + 32 > 1 << 20)
                return ERROR | 9;
            uint64_t range[2] = {d->physical, d->pages * 4096};
            uint32_t kind[2] = {d->type == 7 ? 1 : d->type == 9 ? 3 : d->type == 10 ? 4 : 2, 0};
            memcpy(mb + at, range, 16);
            memcpy(mb + at + 16, kind, 8);
            at += 24;
        }
        *(uint32_t *)(mb + prefix + 4) = (uint32_t)(at - prefix);
        *(uint32_t *)(mb + at) = 0;
        *(uint32_t *)(mb + at + 4) = 8;
        at += 8;
        *(uint32_t *)mb = (uint32_t)at;
        if (!exit_boot(handle, key)) {
            __asm__ volatile("cli; mov %0,%%rdi; jmp *%1" ::"r"(address), "r"(e->entry)
                             : "rdi", "memory");
            __builtin_unreachable();
        }
    }
    return ERROR | 2;
}
