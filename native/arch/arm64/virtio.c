/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "block.h"
#include "platform.h"
#define QN 8
struct descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags, next;
};
struct available {
    uint16_t flags, index, ring[QN], event;
};
struct used {
    uint16_t flags, index;
    struct {
        uint32_t id, length;
    } ring[QN];
};
struct queue {
    struct descriptor *desc;
    struct available *avail;
    volatile struct used *used;
    uint16_t seen;
    unsigned number;
};
struct device {
    struct device *next;
    uintptr_t base;
    uint32_t kind;
    bool flush, readonly;
    struct queue q;
};
static struct device *devices, *rng;
static unsigned transfer_lock;
static uint8_t *bounce;
static void dma_sync(uintptr_t address, size_t n) {
    size_t line = 4ULL << ((sys_read(ctr_el0) >> 16) & 15);
    for (uintptr_t p = address & ~(line - 1); p < address + n; p += line)
        __asm__ volatile("dc civac,%0" ::"r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
static void reg(struct device *d, unsigned off, uint32_t val) {
    mmio_write(d->base + off, val);
}
static uint32_t get(struct device *d, unsigned off) {
    return mmio_read(d->base + off);
}
static void address(struct device *d, unsigned off, uintptr_t p) {
    reg(d, off, p);
    reg(d, off + 4, p >> 32);
}
static bool setup(struct device *d) {
    reg(d, 0x70, 0);
    barrier();
    reg(d, 0x70, 3);
    reg(d, 0x14, 1);
    uint32_t hi = get(d, 0x10);
    reg(d, 0x14, 0);
    uint32_t lo = get(d, 0x10);
    if (!(hi & 1))
        return false;
    d->readonly = d->kind == 2 && (lo & (1u << 5));
    d->flush = d->kind == 2 && (lo & (1u << 9));
    reg(d, 0x24, 1);
    reg(d, 0x20, 1);
    reg(d, 0x24, 0);
    reg(d, 0x20, d->kind == 2 ? (lo & ((1u << 5) | (1u << 9))) : 0);
    reg(d, 0x70, 11);
    if (!(get(d, 0x70) & 8))
        return false;
    reg(d, 0x30, 0);
    if (get(d, 0x34) < QN || get(d, 0x44))
        return false;
    d->q.desc = (void *)page_alloc();
    d->q.avail = (void *)page_alloc();
    d->q.used = (void *)page_alloc();
    reg(d, 0x38, QN);
    address(d, 0x80, (uintptr_t)d->q.desc);
    address(d, 0x90, (uintptr_t)d->q.avail);
    address(d, 0xa0, (uintptr_t)d->q.used);
    d->q.avail->flags = 1;
    dma_sync((uintptr_t)d->q.desc, PAGE);
    dma_sync((uintptr_t)d->q.avail, PAGE);
    dma_sync((uintptr_t)d->q.used, PAGE);
    reg(d, 0x44, 1);
    reg(d, 0x70, 15);
    barrier();
    return true;
}
static void submit(struct device *d, unsigned head) {
    struct queue *q = &d->q;
    q->avail->ring[q->avail->index % QN] = head;
    dma_sync((uintptr_t)q->desc, PAGE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ++q->avail->index;
    dma_sync((uintptr_t)q->avail, PAGE);
    reg(d, 0x50, q->number);
    barrier();
}
static bool complete(struct device *d) {
    struct queue *q = &d->q;
    uint64_t until = sys_read(cntpct_el0) + sys_read(cntfrq_el0) * 3;
    do {
        dma_sync((uintptr_t)q->used, PAGE);
        if (q->used->index != q->seen) {
            unsigned i = q->seen % QN;
            if (q->used->ring[i].id != 0)
                return false;
            ++q->seen;
            reg(d, 0x64, get(d, 0x60));
            return true;
        }
    } while (sys_read(cntpct_el0) < until);
    reg(d, 0x70, 128);
    return false;
}
void arm_virtio_init(void) {
    bounce = (void *)page_alloc();
    for (unsigned i = 0; i < platform.virtio_count; ++i) {
        uintptr_t base = platform.virtio[i];
        if (mmio_read(base) != 0x74726976 || mmio_read(base + 4) != 2)
            continue;
        uint32_t kind = mmio_read(base + 8);
        if (kind != 2 && kind != 4 && kind != 18)
            continue;
        struct device *d = kernel_alloc(sizeof *d);
        memset(d, 0, sizeof *d);
        d->base = base;
        d->kind = kind;
        if (!setup(d)) {
            reg(d, 0x70, 128);
            continue;
        }
        d->next = devices;
        devices = d;
        ++pci_count;
        if (kind == 4)
            rng = d;
        if (kind == 18) {
            uint8_t *events = (void *)page_alloc();
            for (unsigned j = 0; j < QN; ++j) {
                d->q.desc[j] = (struct descriptor){(uintptr_t)events + j * 64, 8, 2, 0};
                submit(d, j);
            }
        }
        log_write("ARM VirtIO-MMIO driver ready, device ");
        log_dec(kind);
        log_write("\n");
    }
}
unsigned block_discover(struct block_device *out, unsigned capacity) {
    unsigned n = 0;
    for (struct device *d = devices; d && n < capacity; d = d->next)
        if (d->kind == 2) {
            uint32_t before, after;
            uint64_t sectors;
            do {
                before = get(d, 0xfc);
                sectors = get(d, 0x100) | ((uint64_t)get(d, 0x104) << 32);
                after = get(d, 0xfc);
            } while (before != after);
            out[n++] = (struct block_device){.transport = (uintptr_t)d, .sectors = sectors};
        }
    return n;
}
static bool request(struct device *d, uint32_t type, uint64_t sector, void *buffer,
                    unsigned bytes) {
    if ((type == 1 || type == 4) && (d->readonly || !d->flush))
        return false;
    uint64_t flags = spin_lock(&transfer_lock);
    bool ok = false;
    /* Dedicated DMA page avoids stack and adjacent dirty cache-line aliases. */
    uint8_t *control = (void *)d->q.desc + 512;
    *(uint32_t *)(control) = type;
    *(uint32_t *)(control + 4) = 0;
    *(uint64_t *)(control + 8) = sector;
    control[64] = 255;
    if (type == 1)
        memcpy(bounce, buffer, bytes);
    dma_sync((uintptr_t)bounce, PAGE);
    d->q.desc[0] = (struct descriptor){(uintptr_t)control, 16, 1, bytes ? 1 : 2};
    d->q.desc[1] =
        (struct descriptor){(uintptr_t)bounce, bytes, (uint16_t)(1 | (type == 0 ? 2 : 0)), 2};
    d->q.desc[2] = (struct descriptor){(uintptr_t)control + 64, 1, 2, 0};
    submit(d, 0);
    if (complete(d)) {
        dma_sync((uintptr_t)control, 128);
        dma_sync((uintptr_t)bounce, PAGE);
        ok = control[64] == 0;
        if (ok && type == 0)
            memcpy(buffer, bounce, bytes);
    }
    spin_unlock(&transfer_lock, flags);
    return ok;
}
bool block_read(struct block_device *b, uint64_t sector, unsigned n, void *p) {
    if (!n || n > 8 || sector > b->sectors || n > b->sectors - sector)
        return false;
    return request((void *)b->transport, 0, sector, p, n * 512);
}
bool block_write(struct block_device *b, uint64_t sector, unsigned n, const void *p) {
    if (!n || n > 8 || sector > b->sectors || n > b->sectors - sector)
        return false;
    return request((void *)b->transport, 1, sector, (void *)p, n * 512);
}
bool block_flush(struct block_device *b) {
    struct device *d = (void *)b->transport;
    return d->flush && request(d, 4, 0, NULL, 0);
}
static bool cpu_random(uint8_t *out, size_t n) {
    // FEAT_RNG is optional even on ARMv8.5; never execute RNDR unconditionally.
    if (!((sys_read(id_aa64isar0_el1) >> 60) & 15))
        return false;
    while (n) {
        uint64_t value = 0;
        unsigned ok = 0;
        for (unsigned retry = 0; retry < 100 && !ok; ++retry)
            __asm__ volatile("mrs %0,S3_3_C2_C4_0; cset %w1,ne" : "=r"(value), "=r"(ok) :: "cc");
        if (!ok)
            return false;
        size_t take = n < 8 ? n : 8;
        memcpy(out, &value, take); out += take; n -= take;
    }
    return true;
}
bool arch_random(uint8_t *out, size_t n) {
    if (!rng) {
        if (arm_firmware_random(out, n) || cpu_random(out, n))
            return true;
        memset(out, 0, n);
        return false;
    }
    if (n > PAGE)
        return false;
    uint64_t flags = spin_lock(&transfer_lock);
    bool ok = true;
    while (n) {
        rng->q.desc[0] = (struct descriptor){(uintptr_t)bounce, (uint32_t)n, 2, 0};
        dma_sync((uintptr_t)bounce, PAGE);
        submit(rng, 0);
        if (!complete(rng)) {
            ok = false;
            break;
        }
        unsigned got = rng->q.used->ring[(rng->q.seen - 1) % QN].length;
        if (!got || got > n) {
            ok = false;
            break;
        }
        dma_sync((uintptr_t)bounce, PAGE);
        memcpy(out, bounce, got);
        out += got;
        n -= got;
    }
    memset(bounce, 0, PAGE);
    spin_unlock(&transfer_lock, flags);
    return ok;
}
static void key(unsigned code, unsigned value) {
    static bool shift;
    static const char keys[] =
        "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char upper[] =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (code == 42 || code == 54) {
        shift = value;
        return;
    }
    if (!value)
        return;
    if (code >= 59 && code <= 65) {
        ui_key(0x81 + code - 59);
        return;
    }
    if (code < sizeof keys && keys[code])
        ui_key(shift ? upper[code] : keys[code]);
}
void arm_virtio_input(void) {
    for (struct device *d = devices; d; d = d->next)
        if (d->kind == 18) {
            struct queue *q = &d->q;
            dma_sync((uintptr_t)q->used, PAGE);
            unsigned budget = QN;
            while (q->seen != q->used->index && budget--) {
                unsigned at = q->seen++ % QN, id = q->used->ring[at].id;
                if (id >= QN)
                    panic("invalid VirtIO input completion");
                uint8_t *event = (void *)(uintptr_t)q->desc[id].address;
                dma_sync((uintptr_t)event, 8);
                uint16_t type = *(uint16_t *)event, code = *(uint16_t *)(event + 2);
                uint32_t value = *(uint32_t *)(event + 4);
                if (type == 1)
                    key(code, value);
                submit(d, id);
                dma_sync((uintptr_t)q->used, PAGE);
            }
            reg(d, 0x64, get(d, 0x60));
        }
}
