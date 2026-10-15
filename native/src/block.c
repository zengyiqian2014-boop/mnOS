/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "block.h"
static uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static void settle(struct block_device *d) { for (unsigned i = 0; i < 4; ++i) (void)inb(d->control); }
static bool wait_ready(struct block_device *d, bool data) {
    for (unsigned n = 0; n < 2000000; ++n) {
        uint8_t status = inb(d->port + 7);
        if (!status || status == 255) return false;
        if (status & 0x80) { __asm__ volatile("pause"); continue; }
        if (status & 0x21) return false;
        if (!data || (status & 8)) return true;
    }
    return false;
}
unsigned ata_discover(struct block_device *devices, unsigned capacity) {
    unsigned count = 0;
    for (unsigned channel = 0; channel < 2; ++channel) for (unsigned slave = 0; slave < 2; ++slave) {
        struct block_device d = {channel ? 0x170 : 0x1f0, channel ? 0x376 : 0x3f6, slave, false, 0};
        outb(d.control, 2); /* Polling only, no IRQ14/15 handler. */
        outb(d.port + 6, 0xa0 | (slave << 4)); settle(&d);
        if (!inb(d.port + 7) || inb(d.port + 7) == 255) continue;
        outb(d.port + 2, 0); outb(d.port + 3, 0); outb(d.port + 4, 0); outb(d.port + 5, 0);
        outb(d.port + 7, 0xec); settle(&d);
        if (!wait_ready(&d, true) || inb(d.port + 4) || inb(d.port + 5)) continue; /* ATAPI ignored. */
        uint16_t id[256]; for (unsigned i = 0; i < 256; ++i) id[i] = inw(d.port);
        if (!(id[49] & (1u << 9))) continue;
        if ((id[106] & 0xc000) == 0x4000 && (id[106] & (1u << 12)) &&
            ((uint32_t)id[117] | ((uint32_t)id[118] << 16)) != 256) continue; /* 512-byte sectors only. */
        d.lba48 = (id[83] & 0xc000) == 0x4000 && (id[83] & (1u << 10));
        d.sectors = d.lba48 ? (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
            ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48) : id[60] | ((uint32_t)id[61] << 16);
        if (!d.sectors || d.sectors > (d.lba48 ? 1ULL << 48 : 1ULL << 28)) continue;
        if (count < capacity) devices[count++] = d;
    }
    return count;
}
static bool transfer(struct block_device *d, uint64_t lba, unsigned count, void *buffer, bool write) {
    if (!count || count > 255 || lba >= d->sectors || count > d->sectors - lba) return false;
    outb(d->port + 6, 0xe0 | (d->drive << 4) | (d->lba48 ? 0 : (lba >> 24) & 15)); settle(d);
    if (!wait_ready(d, false)) return false;
    if (d->lba48) {
        outb(d->port + 2, 0); outb(d->port + 3, lba >> 24);
        outb(d->port + 4, lba >> 32); outb(d->port + 5, lba >> 40);
    }
    outb(d->port + 2, count); outb(d->port + 3, lba); outb(d->port + 4, lba >> 8); outb(d->port + 5, lba >> 16);
    outb(d->port + 7, d->lba48 ? (write ? 0x34 : 0x24) : (write ? 0x30 : 0x20));
    uint8_t *bytes = buffer;
    while (count--) {
        settle(d); if (!wait_ready(d, true)) return false;
        for (unsigned i = 0; i < 256; ++i) {
            uint16_t value;
            if (write) { memcpy(&value, bytes, 2); outw(d->port, value); }
            else { value = inw(d->port); memcpy(bytes, &value, 2); }
            bytes += 2;
        }
    }
    settle(d); return wait_ready(d, false);
}
bool ata_read(struct block_device *d, uint64_t lba, unsigned n, void *p) { return transfer(d, lba, n, p, false); }
bool ata_write(struct block_device *d, uint64_t lba, unsigned n, const void *p) { return transfer(d, lba, n, (void *)p, true); }
bool ata_flush(struct block_device *d) {
    outb(d->port + 6, 0xe0 | (d->drive << 4)); settle(d);
    if (!wait_ready(d, false)) return false;
    outb(d->port + 7, d->lba48 ? 0xea : 0xe7); settle(d);
    return wait_ready(d, false);
}
