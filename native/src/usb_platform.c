/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usb_platform.h"
#include "kernel.h"
#ifdef __aarch64__
#include "../arch/arm64/platform.h"
static bool pci_ready;
void usb_platform_init(void) {
    if (platform.pci_ecam && platform.pci_size)
        pci_ready = usb_map_mmio(platform.pci_ecam, platform.pci_size) != NULL;
}
uint64_t usb_millis(void) {
    return sys_read(cntpct_el0) / (sys_read(cntfrq_el0) / 1000);
}
void usb_dma_sync(void *p, size_t size) {
    size_t line = 4ULL << ((sys_read(ctr_el0) >> 16) & 15);
    uintptr_t start = (uintptr_t)p;
    for (uintptr_t at = start & ~(line - 1); at < start + size; at += line)
        __asm__ volatile("dc civac,%0" ::"r"(at) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
bool usb_pci_window(unsigned *start, unsigned *end) {
    if (!pci_ready || platform.pci_size < 1048576)
        return false;
    *start = platform.pci_first;
    *end = platform.pci_last;
    return true;
}
static uintptr_t config(unsigned bus, unsigned slot, unsigned fn, unsigned reg) {
    return platform.pci_ecam + ((uintptr_t)(bus - platform.pci_first) << 20) + (slot << 15) +
           (fn << 12) + (reg & 0xffc);
}
uint32_t usb_pci_read(unsigned b, unsigned s, unsigned f, unsigned r) {
    return mmio_read(config(b, s, f, r));
}
void usb_pci_write(unsigned b, unsigned s, unsigned f, unsigned r, uint32_t v) {
    mmio_write(config(b, s, f, r), v);
}
#else
static uint64_t cycles_per_ms;
static uint64_t counter(void) {
    uint32_t a, d;
    __asm__ volatile("lfence; rdtsc" : "=a"(a), "=d"(d)::"memory");
    return (uint64_t)d << 32 | a;
}
void usb_platform_init(void) {
    uint32_t a, b, c, d, max;
    cpuid(0, 0, &max, &b, &c, &d);
    if (max >= 0x15) {
        cpuid(0x15, 0, &a, &b, &c, &d);
        if (a && b && c)
            cycles_per_ms = (uint64_t)c * b / a / 1000;
    }
    if (!cycles_per_ms && max >= 0x16) {
        cpuid(0x16, 0, &a, &b, &c, &d);
        cycles_per_ms = (uint64_t)(a & 65535) * 1000;
    }
    if (!cycles_per_ms) {
        uint8_t gate = inb(0x61);
        outb(0x61, gate & ~3u);
        outb(0x43, 0xb0);
        outb(0x42, 23864 & 255);
        outb(0x42, 23864 >> 8);
        uint64_t begin = counter();
        outb(0x61, (gate & ~2u) | 1);
        unsigned limit = 10000000;
        while (!(inb(0x61) & 32) && --limit)
            __asm__ volatile("pause");
        uint64_t elapsed = counter() - begin;
        outb(0x61, gate);
        if (limit && elapsed >= 20000)
            cycles_per_ms = elapsed / 20;
    }
    if (!cycles_per_ms)
        cycles_per_ms = 1000000;
}
uint64_t usb_millis(void) {
    return counter() / cycles_per_ms;
}
void usb_dma_sync(void *p, size_t n) {
    (void)p;
    (void)n;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
bool usb_pci_window(unsigned *start, unsigned *end) {
    *start = 0;
    *end = 255;
    return true;
}
uint32_t usb_pci_read(unsigned b, unsigned s, unsigned f, unsigned r) {
    outl(0xcf8, 0x80000000u | (b << 16) | (s << 11) | (f << 8) | (r & 0xfc));
    return inl(0xcfc);
}
void usb_pci_write(unsigned b, unsigned s, unsigned f, unsigned r, uint32_t v) {
    outl(0xcf8, 0x80000000u | (b << 16) | (s << 11) | (f << 8) | (r & 0xfc));
    outl(0xcfc, v);
}
#endif
void *usb_map_mmio(uint64_t p, size_t n) {
    return memory_map_device(p, n);
}
void usb_delay(unsigned ms) {
    uint64_t end = usb_millis() + ms;
    while (usb_millis() < end)
        __asm__ volatile("" ::: "memory");
}
