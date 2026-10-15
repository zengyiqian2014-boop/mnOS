/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "phys.h"
#include "platform.h"
#define ADDR 0x0000fffffffff000ULL
#define AF (1ULL << 10)
#define SH (3ULL << 8)
#define RO (1ULL << 7)
#define USER (1ULL << 6)
#define PXN (1ULL << 53)
#define UXN (1ULL << 54)
#define RAM (AF | SH)
#define DEVICE (AF | PXN | UXN | (1ULL << 2))
static struct phys_allocator physical;
static unsigned allocator_lock;
uint64_t arm_root, arm_tcr, kernel_pml4;
static uintptr_t heap_next = 0x600000000000ULL;
void *phys_pointer(uint64_t p) {
    return (void *)(uintptr_t)p;
}
static void *map_physical(uint64_t p, void *unused) {
    (void)unused;
    return phys_pointer(p);
}
uint64_t memory_limit(void) {
    return physical.limit;
}
uint64_t pages_free(void) {
    return physical.free_pages;
}
uint64_t pages_total(void) {
    return physical.total_pages;
}
uint64_t page_alloc_between(uint64_t low, uint64_t high) {
    uint64_t flags = spin_lock(&allocator_lock);
    uint64_t p = phys_alloc(&physical, low, high);
    spin_unlock(&allocator_lock, flags);
    if (!p)
        panic("physical pages exhausted");
    memset((void *)p, 0, PAGE);
    return p;
}
uint64_t page_alloc(void) {
    return page_alloc_between(0, physical.limit);
}
void page_free(uint64_t p) {
    uint64_t flags = spin_lock(&allocator_lock);
    bool ok = phys_release(&physical, p);
    spin_unlock(&allocator_lock, flags);
    if (!ok)
        panic("invalid page release");
}
static uint64_t *table(uint64_t *entry, unsigned shift) {
    if (!(*entry & 1))
        *entry = page_alloc() | 3;
    else if ((*entry & 3) == 1) {
        uint64_t old = *entry, *next = (void *)page_alloc();
        uint64_t base = old & ADDR, flags = old & ~ADDR;
        for (unsigned i = 0; i < 512; ++i)
            next[i] = (base + ((uint64_t)i << shift)) | flags | (shift == 12 ? 2 : 0);
        *entry = (uintptr_t)next | 3;
    }
    return (void *)(*entry & ADDR);
}
void arm_map(uint64_t *root, uint64_t va, uint64_t pa, uint64_t flags) {
    for (unsigned shift = 39; shift > 12; shift -= 9)
        root = table(&root[(va >> shift) & 511], shift - 9);
    root[(va >> 12) & 511] = (pa & ADDR) | flags | 3;
}
static void region(uint64_t start, uint64_t size, uint64_t flags) {
    uint64_t end = start + size;
    if (end < start || end > physical.limit)
        end = physical.limit;
    start &= ~(PAGE - 1);
    for (uint64_t p = start; p < end;) {
        uint64_t *l1 = table(&((uint64_t *)arm_root)[(p >> 39) & 511], 30);
        if (!(p & ((1ULL << 30) - 1)) && end - p >= (1ULL << 30) && !(l1[(p >> 30) & 511] & 1)) {
            l1[(p >> 30) & 511] = p | flags | 1;
            p += 1ULL << 30;
            continue;
        }
        uint64_t *l2 = table(&l1[(p >> 30) & 511], 21);
        if (!(p & ((1ULL << 21) - 1)) && end - p >= (1ULL << 21) && !(l2[(p >> 21) & 511] & 1)) {
            l2[(p >> 21) & 511] = p | flags | 1;
            p += 1ULL << 21;
            continue;
        }
        arm_map((void *)arm_root, p, p, flags);
        p += PAGE;
    }
}
void *kernel_alloc(size_t bytes) {
    if (!bytes || bytes > (1ULL << 30))
        panic("kernel allocation size");
    size_t pages = (bytes + PAGE - 1) / PAGE;
    uintptr_t base = heap_next;
    heap_next += (pages + 1) * PAGE;
    for (size_t i = 0; i < pages; ++i)
        arm_map((void *)arm_root, base + i * PAGE, page_alloc(), RAM | PXN | UXN);
    barrier();
    return (void *)base;
}
void arm_mmu_on(uint64_t root) {
    sys_write(mair_el1, 0x04ff);
    sys_write(tcr_el1, arm_tcr);
    sys_write(ttbr0_el1, root);
    sys_write(ttbr1_el1, 0);
    __asm__ volatile("dsb sy; tlbi vmalle1; dsb sy; isb" ::: "memory");
    /* RES1 plus MMU, caches, alignment, WXN. No firmware tables survive. */
    sys_write(sctlr_el1, 0x30d00800ULL | 1 | 4 | (1 << 12) | (1 << 19));
    barrier();
}
void arm_cache_code(uintptr_t p, size_t n) {
    uint64_t ctr = sys_read(ctr_el0);
    size_t dc = 4ULL << ((ctr >> 16) & 15), ic = 4ULL << (ctr & 15);
    for (uintptr_t a = p & ~(dc - 1); a < p + n; a += dc)
        __asm__ volatile("dc cvau,%0" ::"r"(a) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (uintptr_t a = p & ~(ic - 1); a < p + n; a += ic)
        __asm__ volatile("ic ivau,%0" ::"r"(a) : "memory");
    barrier();
}
void memory_init(void) {
    static const unsigned bits[] = {32, 36, 40, 42, 44, 48, 52};
    unsigned pa = sys_read(id_aa64mmfr0_el1) & 15;
    if (pa >= ARRAY_SIZE(bits) || ((sys_read(id_aa64mmfr0_el1) >> 28) & 15) == 15)
        panic("unsupported address format");
    uint64_t cap = 1ULL << bits[pa];
    if (cap > PHYS_CAP)
        cap = PHYS_CAP;
    phys_reset(&physical, cap, map_physical, NULL);
    for (unsigned i = 0; i < platform.ram_count; ++i)
        if (!phys_add(&physical, platform.ram[i].start, platform.ram[i].size))
            panic("RAM ranges");
    for (unsigned i = 0; i < platform.reserved_count; ++i)
        if (!phys_reserve(&physical, platform.reserved[i].start, platform.reserved[i].size))
            panic("reserved ranges");
    if (!phys_reserve(&physical, platform.image_start, platform.image_end - platform.image_start) ||
        !phys_reserve(&physical, (uintptr_t)platform.fdt, platform.fdt_size) ||
        !phys_prepare(&physical))
        panic("physical allocator");
    arm_root = kernel_pml4 = page_alloc();
    arm_tcr = 16 | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (1ULL << 23) |
              ((uint64_t)(pa > 5 ? 5 : pa) << 32);
    for (unsigned i = 0; i < platform.ram_count; ++i)
        region(platform.ram[i].start, platform.ram[i].size, RAM | PXN | UXN);
    region(platform.image_start, platform.image_end - platform.image_start, RAM | PXN | UXN);
    for (uintptr_t p = platform.text_start; p < platform.text_end; p += PAGE)
        arm_map((void *)arm_root, p, p, RAM | RO | UXN);
    for (uintptr_t p = platform.ro_start; p < platform.ro_end; p += PAGE)
        arm_map((void *)arm_root, p, p, RAM | RO | PXN | UXN);
    region(platform.uart, 4096, DEVICE);
    region(platform.gicd, 65536, DEVICE);
    if (platform.gic_version == 2)
        region(platform.gicc, 8192, DEVICE);
    else
        region(platform.gicr, platform.gicr_size, DEVICE);
    for (unsigned i = 0; i < platform.virtio_count; ++i)
        region(platform.virtio[i], 512, DEVICE);
    if (boot.fb.pixels)
        region((uintptr_t)boot.fb.pixels, (uint64_t)boot.fb.pitch * boot.fb.height, DEVICE);
    /* Every root shares the heap's lower tables before task roots are cloned. */
    arm_map((void *)arm_root, heap_next, page_alloc(), RAM | PXN | UXN);
    heap_next += 2 * PAGE;
    arm_mmu_on(arm_root);
    sys_write(vbar_el1, (uintptr_t)arm_vectors);
    barrier();
    uint64_t p = page_alloc();
    page_free(p);
    if (phys_release(&physical, p))
        panic("allocator double free accepted");
    uint64_t thresholds[] = {1ULL << 32, 1ULL << 45};
    for (unsigned i = 0; i < ARRAY_SIZE(thresholds); ++i) {
        uint64_t high = phys_alloc(&physical, thresholds[i], physical.limit);
        if (high) {
            volatile uint64_t *probe = (void *)high;
            *probe = 0x6d6e6f7361726d36ULL;
            if (*probe != 0x6d6e6f7361726d36ULL)
                panic("high memory read/write");
            page_free(high);
            log_write("ARM high RAM read/write PASS at ");
            log_hex(high);
            log_write("\n");
        }
    }
    log_write(
        "ARM MMU PASS: 4 KiB pages / EL0 isolation / W^X / null and stack guards; physical limit ");
    log_dec(cap >> 40);
    log_write(" TiB\n");
}
uint64_t address_space(uint64_t code, uint64_t data, uint64_t stack) {
    uint64_t *root = (void *)page_alloc();
    memcpy(root, (void *)arm_root, PAGE);
    arm_map(root, USER_BASE, code, RAM | USER | RO | PXN);
    arm_map(root, USER_DATA, data, RAM | USER | PXN | UXN);
    arm_map(root, USER_STACK, stack, RAM | USER | PXN | UXN);
    return (uintptr_t)root;
}
