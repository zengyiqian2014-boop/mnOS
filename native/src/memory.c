/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#define MAX_PAGES (0x100000000ULL / PAGE)
#define NX (1ULL << 63)
static uint64_t used[MAX_PAGES / 64];
static uint64_t allocated[MAX_PAGES / 64];
static uint64_t free_pages, total_pages, next_page;
uint64_t kernel_pml4;
static uint64_t kernel_pdpt;
extern char kernel_start[], kernel_end[], text_start[], text_end[], rodata_start[], rodata_end[],
    boot_stack_guard[], irq_stack_area[];
static bool occupied(uint64_t p) {
    return (used[p / 64] >> (p % 64)) & 1;
}
static void set(uint64_t p) {
    if (!occupied(p)) {
        used[p / 64] |= 1ULL << (p % 64);
        --free_pages;
    }
}
static void clear(uint64_t p) {
    if (occupied(p)) {
        used[p / 64] &= ~(1ULL << (p % 64));
        ++free_pages;
    }
}
static void reserve(uint64_t start, uint64_t end) {
    for (uint64_t p = start / PAGE; p < (end + PAGE - 1) / PAGE && p < MAX_PAGES; ++p)
        set(p);
}
uint64_t pages_free(void) {
    return free_pages;
}
uint64_t pages_total(void) {
    return total_pages;
}
uint64_t page_alloc(void) {
    for (uint64_t n = 0; n < MAX_PAGES; ++n) {
        uint64_t p = (next_page + n) % MAX_PAGES;
        if (!occupied(p)) {
            set(p);
            allocated[p / 64] |= 1ULL << (p % 64);
            next_page = p + 1;
            uint64_t addr = p * PAGE;
            memset((void *)addr, 0, PAGE);
            return addr;
        }
    }
    panic("physical memory exhausted");
}
void page_free(uint64_t address) {
    if ((address & (PAGE - 1)) || address < (uint64_t)kernel_end || address >= 0x100000000ULL ||
        !occupied(address / PAGE))
        panic("invalid or double page free");
    uint64_t p = address / PAGE;
    if (!(allocated[p / 64] & (1ULL << (p % 64))))
        panic("free of reserved physical page");
    allocated[p / 64] &= ~(1ULL << (p % 64));
    clear(address / PAGE);
}
static uint64_t new_table(void) {
    return page_alloc();
}
static void verify_kernel_mapping(void) {
    uint64_t cr0, efer;
    uint32_t lo, hi;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    efer = ((uint64_t)hi << 32) | lo;
    if (!(cr0 & (1ULL << 16)) || !(efer & (1ULL << 11)))
        panic("write protection or NX disabled");
    uint64_t *pd = (void *)(((uint64_t *)kernel_pdpt)[0] & 0x000ffffffffff000ULL);
    for (uint64_t addr = 0; addr < (uint64_t)kernel_end; addr += PAGE) {
        uint64_t *pt = (void *)(pd[addr >> 21] & 0x000ffffffffff000ULL),
                 entry = pt[(addr >> 12) & 511];
        if (addr == 0 || addr == (uint64_t)boot_stack_guard || addr == (uint64_t)irq_stack_area) {
            if (entry)
                panic("kernel guard mapped");
            continue;
        }
        bool code = addr >= (uint64_t)text_start && addr < (uint64_t)text_end;
        bool ro = addr >= (uint64_t)rodata_start && addr < (uint64_t)rodata_end;
        if (!(entry & 1) || (entry & 4) || ((entry & 2) != 0) == (code || ro) ||
            ((entry & NX) != 0) == code)
            panic("kernel page permission mismatch");
    }
}
void memory_init(void) {
    memset(used, 0xff, sizeof used);
    for (uintptr_t p = (uintptr_t)boot.map + 16;
         p + sizeof(struct mb_mmap_entry) <= (uintptr_t)boot.map + boot.map->tag.size;
         p += boot.map->entry_size) {
        struct mb_mmap_entry *e = (void *)p;
        if (e->type != 1 || e->address >= 0x100000000ULL)
            continue;
        uint64_t end =
            e->length > 0x100000000ULL - e->address ? 0x100000000ULL : e->address + e->length;
        for (uint64_t i = (e->address + PAGE - 1) / PAGE; i < end / PAGE; ++i)
            clear(i);
    }
    total_pages = free_pages;
    reserve(0, (uint64_t)kernel_end);
    reserve(boot.mb_start, boot.mb_end);
    next_page = ((uint64_t)kernel_end + PAGE - 1) / PAGE;
    if (!free_pages || (uint64_t)kernel_end >= 32 * 1024 * 1024)
        panic("unsupported memory layout");
    uint64_t before = free_pages, p = page_alloc();
    if (p & (PAGE - 1))
        panic("unaligned allocation");
    page_free(p);
    if (free_pages != before)
        panic("allocator accounting");
    kernel_pml4 = new_table();
    kernel_pdpt = new_table();
    ((uint64_t *)kernel_pml4)[0] = kernel_pdpt | 3;
    for (unsigned gig = 0; gig < 4; ++gig) {
        uint64_t pd = new_table();
        ((uint64_t *)kernel_pdpt)[gig] = pd | 3;
        for (unsigned block = 0; block < 512; ++block) {
            uint64_t physical = ((uint64_t)gig << 30) + ((uint64_t)block << 21);
            if (physical < 32 * 1024 * 1024) {
                uint64_t pt = new_table();
                ((uint64_t *)pd)[block] = pt | 3;
                for (unsigned i = 0; i < 512; ++i) {
                    uint64_t addr = physical + i * PAGE, flags = 3 | NX;
                    if (addr >= (uint64_t)text_start && addr < (uint64_t)text_end)
                        flags = 1;
                    if (addr >= (uint64_t)rodata_start && addr < (uint64_t)rodata_end)
                        flags = 1 | NX;
                    if (addr == 0 || addr == (uint64_t)boot_stack_guard ||
                        addr == (uint64_t)irq_stack_area)
                        continue;
                    ((uint64_t *)pt)[i] = addr | flags;
                }
            } else
                ((uint64_t *)pd)[block] =
                    physical | 0x83 | NX | (physical >= 0xf0000000ULL ? 0x18 : 0);
        }
    }
    cr3_set(kernel_pml4);
    uint64_t cr4;
    __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
    if (cpu_smep)
        cr4 |= 1ULL << 20;
    if (cpu_smap)
        cr4 |= 1ULL << 21;
    __asm__ volatile("mov %0,%%cr4" ::"r"(cr4) : "memory");
    verify_kernel_mapping();
}
uint64_t address_space(uint64_t code, uint64_t data, uint64_t stack) {
    uint64_t pml4 = new_table(), pdpt = new_table(), pd = new_table(), pt = new_table();
    ((uint64_t *)pml4)[0] = pdpt | 7;
    memcpy((void *)pdpt, (void *)kernel_pdpt, 4 * sizeof(uint64_t));
    ((uint64_t *)pdpt)[4] = pd | 7;
    ((uint64_t *)pd)[0] = pt | 7;
    ((uint64_t *)pt)[0] = code | 5;
    ((uint64_t *)pt)[1] = data | 7 | NX;
    ((uint64_t *)pt)[3] = stack | 7 | NX;
    return pml4;
}
