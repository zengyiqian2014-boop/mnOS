/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "phys.h"
#define NX (1ULL << 63)
#define ADDRESS 0x000ffffffffff000ULL
#define DIRECT 0xffff800000000000ULL
static struct phys_allocator physical;
static bool direct_ready, huge_gigabyte;
static unsigned physical_bits;
static unsigned physical_lock, virtual_lock;
static uint64_t kernel_pdpt, virtual_next = 0xffffc00000000000ULL;
uint64_t kernel_pml4;
extern char kernel_end[], text_start[], text_end[], rodata_start[], rodata_end[],
    boot_stack_guard[], irq_stack_area[];

void *phys_pointer(uint64_t address) {
    if (address >= physical.limit) panic("physical pointer exceeds CPU address width");
    if (address < (1ULL << 32)) return (void *)address;
    if (!direct_ready) panic("high physical address before direct map");
    return (void *)(DIRECT + address);
}
static void *map_metadata(uint64_t p, void *unused) {
    (void)unused;
    return phys_pointer(p);
}
uint64_t pages_free(void) {
    uint64_t flags = spin_lock(&physical_lock), free = physical.free_pages;
    spin_unlock(&physical_lock, flags);
    return free;
}
uint64_t pages_total(void) { return physical.total_pages; }
uint64_t memory_limit(void) { return physical.limit; }
const void *firmware_pointer(uint64_t address, size_t length) {
    if (address < PAGE || address >= physical.limit || length > physical.limit - address) return NULL;
    for (uintptr_t p = (uintptr_t)boot.map + 16; p < (uintptr_t)boot.map + boot.map->tag.size;
         p += boot.map->entry_size) {
        const struct mb_mmap_entry *e = (void *)p;
        if (address < e->address || address - e->address >= e->length ||
            length > e->length - (address - e->address)) continue;
        if (address < (1ULL << 32) && length <= (1ULL << 32) - address) return phys_pointer(address);
        if (e->type == 3 || e->type == 4) return phys_pointer(address);
    }
    return NULL;
}
uint64_t page_alloc_between(uint64_t min, uint64_t max) {
    uint64_t flags = spin_lock(&physical_lock);
    uint64_t p = phys_alloc(&physical, min, max);
    spin_unlock(&physical_lock, flags);
    if (p) memset(phys_pointer(p), 0, PAGE);
    return p;
}
uint64_t page_alloc(void) {
    uint64_t p = page_try_alloc();
    if (!p) panic("physical memory exhausted");
    return p;
}
uint64_t page_try_alloc(void) {
    uint64_t p = direct_ready ? page_alloc_between(1ULL << 32, physical.limit) : 0;
    if (!p) p = page_alloc_between(0, 1ULL << 32);
    return p;
}
void page_free(uint64_t p) {
    uint64_t flags = spin_lock(&physical_lock);
    if (!phys_release(&physical, p)) panic("invalid, reserved or double page free");
    spin_unlock(&physical_lock, flags);
}
static uint64_t *table(uint64_t p) { return phys_pointer(p & ADDRESS); }
static uint64_t child(uint64_t *parent, unsigned index) {
    if (!(parent[index] & 1)) parent[index] = page_alloc() | 3;
    if (parent[index] & 0x80) panic("large-page/table collision");
    return parent[index] & ADDRESS;
}
static void map_leaf(uint64_t virtual, uint64_t physical_address, uint64_t size, uint64_t flags) {
    uint64_t pdpt = child(table(kernel_pml4), (virtual >> 39) & 511);
    uint64_t *entry = &table(pdpt)[(virtual >> 30) & 511];
    if (size != (1ULL << 30)) {
        uint64_t pd = child(table(pdpt), (virtual >> 30) & 511);
        entry = &table(pd)[(virtual >> 21) & 511];
        if (size != (1ULL << 21)) {
            uint64_t pt = child(table(pd), (virtual >> 21) & 511);
            entry = &table(pt)[(virtual >> 12) & 511];
        }
    }
    uint64_t value = physical_address | flags | (size == PAGE ? 0 : 0x80);
    if (*entry && *entry != value) panic("conflicting direct-map ranges");
    *entry = value;
}
static void map_range(uint64_t start, uint64_t end, uint64_t flags) {
    if (start < (1ULL << 32)) start = 1ULL << 32;
    if (end > physical.limit) end = physical.limit;
    start = (start + PAGE - 1) & ~(PAGE - 1);
    end &= ~(PAGE - 1);
    while (start < end) {
        uint64_t size = PAGE;
        if (huge_gigabyte && !(start & ((1ULL << 30) - 1)) && end - start >= (1ULL << 30))
            size = 1ULL << 30;
        else if (!(start & ((1ULL << 21) - 1)) && end - start >= (1ULL << 21)) size = 1ULL << 21;
        map_leaf(DIRECT + start, start, size, flags | NX);
        start += size;
    }
}
void *kernel_alloc(size_t bytes) {
    uint64_t flags = spin_lock(&virtual_lock);
    if (!bytes || bytes > (1ULL << 40) || virtual_next > 0xffffe00000000000ULL - bytes - 2 * PAGE)
        panic("kernel virtual allocation overflow");
    virtual_next += PAGE; /* Unmapped guard preceding each allocation. */
    uint64_t start = virtual_next;
    bytes = (bytes + PAGE - 1) & ~(PAGE - 1);
    for (size_t offset = 0; offset < bytes; offset += PAGE)
        map_leaf(start + offset, page_alloc(), PAGE, 3 | NX);
    virtual_next += bytes;
    spin_unlock(&virtual_lock, flags);
    return (void *)start;
}
void memory_protect_low(uint64_t address, bool executable) {
    if (address % PAGE || address >= 64 * 1024 * 1024) panic("invalid bootstrap page protection");
    uint64_t *pd = table(table(kernel_pdpt)[0]);
    uint64_t *pt = table(pd[address >> 21]);
    pt[(address >> 12) & 511] = address | (executable ? 1 : (3 | NX));
    __asm__ volatile("invlpg (%0)" :: "r"(address) : "memory");
}
void memory_cpu_init(void) {
    uint64_t cr4, cr0;
    uint32_t lo, hi;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    if (!(cr0 & (1ULL << 16)) || !(lo & (1u << 11))) panic("CPU write protection or NX disabled");
    __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
    if (cpu_smep) cr4 |= 1ULL << 20;
    if (cpu_smap) cr4 |= 1ULL << 21;
    __asm__ volatile("mov %0,%%cr4" :: "r"(cr4) : "memory");
}
static void verify_kernel_mapping(void) {
    uint64_t cr0;
    uint32_t lo, hi;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    if (!(cr0 & (1ULL << 16)) || !(lo & (1u << 11))) panic("write protection or NX disabled");
    uint64_t *pd = table(table(kernel_pdpt)[0]);
    for (uint64_t addr = 0; addr < (uint64_t)kernel_end; addr += PAGE) {
        uint64_t entry = table(pd[addr >> 21])[(addr >> 12) & 511];
        if (!addr || addr == (uint64_t)boot_stack_guard || addr == (uint64_t)irq_stack_area) {
            if (entry) panic("kernel guard mapped");
            continue;
        }
        bool code = addr >= (uint64_t)text_start && addr < (uint64_t)text_end;
        bool ro = addr >= (uint64_t)rodata_start && addr < (uint64_t)rodata_end;
        if (!(entry & 1) || (entry & 4) || ((entry & 2) != 0) == (code || ro) ||
            ((entry & NX) != 0) == code) panic("kernel page permission mismatch");
    }
}
static void reserve(uint64_t start, uint64_t length) {
    if (!phys_reserve(&physical, start, length)) panic("too many physical memory reservations");
}
void memory_init(void) {
    uint32_t a, b, c, d, maximum;
    cpuid(0x80000000, 0, &maximum, &b, &c, &d);
    physical_bits = 36;
    if (maximum >= 0x80000008) {
        cpuid(0x80000008, 0, &a, &b, &c, &d);
        physical_bits = a & 255;
        if (physical_bits < 32 || physical_bits > 52) panic("invalid CPU physical address width");
    }
    cpuid(0x80000001, 0, &a, &b, &c, &d);
    huge_gigabyte = (d & (1u << 26)) != 0;
    phys_reset(&physical, 1ULL << physical_bits, map_metadata, NULL);
    for (uintptr_t p = (uintptr_t)boot.map + 16; p < (uintptr_t)boot.map + boot.map->tag.size;
         p += boot.map->entry_size) {
        const struct mb_mmap_entry *e = (void *)p;
        if (e->type == 1 && !phys_add(&physical, e->address, e->length))
            panic("too many physical memory ranges");
    }
    /* Reserved firmware entries take precedence, regardless of input ordering. */
    for (uintptr_t p = (uintptr_t)boot.map + 16; p < (uintptr_t)boot.map + boot.map->tag.size;
         p += boot.map->entry_size) {
        const struct mb_mmap_entry *e = (void *)p;
        if (e->type != 1) reserve(e->address, e->length);
    }
    reserve(0, (uint64_t)kernel_end);
    reserve(boot.mb_start, boot.mb_end - boot.mb_start);
    if (boot.fb.pixels) reserve((uintptr_t)boot.fb.pixels, (uint64_t)boot.fb.pitch * boot.fb.height);
    for (uintptr_t p = boot.mb_start + 8; p + 8 <= boot.mb_end;) {
        const struct mb_tag *t = (void *)p;
        if (!t->type) break;
        if (t->type == 3 && t->size >= 16) {
            const uint32_t *module = (void *)(p + 8);
            if (module[1] < module[0]) panic("invalid boot module extent");
            reserve(module[0], (uint64_t)module[1] - module[0]);
        }
        p += (t->size + 7ULL) & ~7ULL;
    }
    if ((uint64_t)kernel_end >= 64 * 1024 * 1024 || !phys_prepare(&physical))
        panic("unsupported bootstrap memory layout");
    kernel_pml4 = page_alloc();
    kernel_pdpt = page_alloc();
    table(kernel_pml4)[0] = kernel_pdpt | 3;
    for (unsigned gig = 0; gig < 4; ++gig) {
        uint64_t pd = page_alloc();
        table(kernel_pdpt)[gig] = pd | 3;
        for (unsigned block = 0; block < 512; ++block) {
            uint64_t base = ((uint64_t)gig << 30) + ((uint64_t)block << 21);
            if (base < 64 * 1024 * 1024) {
                uint64_t pt = page_alloc();
                table(pd)[block] = pt | 3;
                for (unsigned i = 0; i < 512; ++i) {
                    uint64_t addr = base + i * PAGE, flags = 3 | NX;
                    if (addr >= (uint64_t)text_start && addr < (uint64_t)text_end) flags = 1;
                    if (addr >= (uint64_t)rodata_start && addr < (uint64_t)rodata_end) flags = 1 | NX;
                    if (!addr || addr == (uint64_t)boot_stack_guard || addr == (uint64_t)irq_stack_area) continue;
                    table(pt)[i] = addr | flags;
                }
            } else table(pd)[block] = base | 0x83 | NX | (base >= 0xf0000000ULL ? 0x18 : 0);
        }
    }
    for (size_t i = 0; i < physical.count; ++i)
        map_range(physical.ranges[i].start, physical.ranges[i].end, 3);
    for (uintptr_t p = (uintptr_t)boot.map + 16; p < (uintptr_t)boot.map + boot.map->tag.size;
         p += boot.map->entry_size) {
        const struct mb_mmap_entry *e = (void *)p;
        if ((e->type == 3 || e->type == 4) && e->address < physical.limit) {
            uint64_t end = e->length > physical.limit - e->address ? physical.limit : e->address + e->length;
            map_range(e->address, end, 1);
        }
    }
    cr3_set(kernel_pml4);
    direct_ready = true;
    memory_cpu_init();
    verify_kernel_mapping();
    uint64_t before = physical.free_pages, meta_before = physical.metadata_pages;
    uint64_t page = page_alloc();
    page_free(page);
    if (physical.free_pages + physical.metadata_pages - meta_before != before)
        panic("allocator accounting");
    uint64_t highest = physical.ranges[physical.count - 1].end;
    uint64_t minimum = highest > (1ULL << 45) ? (1ULL << 45) : (1ULL << 32);
    page = page_alloc_between(minimum, physical.limit);
    if (page) {
        uint64_t low = page_alloc_between(32ULL << 20, 1ULL << 32);
        uint64_t last = page_alloc_between(highest - PAGE, highest);
        if (!low || !last || page == last) panic("high memory probe allocation");
        volatile uint64_t *probe = phys_pointer(page), *low_probe = phys_pointer(low),
                          *last_probe = phys_pointer(last);
        *low_probe = 0x123456789abcdef0ULL;
        *last_probe = 0xfedcba9876543210ULL;
        probe[0] = 0xc001c0de55aa1122ULL;
        probe[511] = ~probe[0];
        if (probe[0] != 0xc001c0de55aa1122ULL || probe[511] != ~probe[0] ||
            *low_probe != 0x123456789abcdef0ULL || *last_probe != 0xfedcba9876543210ULL)
            panic("high physical RAM read/write alias");
        log_write("HIGHMEM PASS physical="); log_hex(page);
        log_write(" last="); log_hex(last); log_write(" read/write/free without low alias\n");
        page_free(page);
        page_free(low);
        page_free(last);
    }
    log_write("MEMORY: address_limit_GiB="); log_dec(physical.limit >> 30);
    log_write(" managed_pages="); log_dec(physical.total_pages);
    log_write(" metadata_pages="); log_dec(physical.metadata_pages);
    log_write(" huge_1G="); log_dec(huge_gigabyte); log_write("\n");
}
uint64_t address_space(uint64_t code, uint64_t data, uint64_t stack) {
    uint64_t root = address_space_try(code, data, stack);
    if (!root) panic("task page tables exhausted");
    return root;
}
uint64_t address_space_try(uint64_t code, uint64_t data, uint64_t stack) {
    uint64_t pages[4];
    for (unsigned i = 0; i < 4; ++i) {
        pages[i] = page_try_alloc();
        if (!pages[i]) { while (i) page_free(pages[--i]); return 0; }
    }
    uint64_t pml4 = pages[0], pdpt = pages[1], pd = pages[2], pt = pages[3];
    memcpy(table(pml4), table(kernel_pml4), PAGE);
    table(pml4)[0] = pdpt | 7;
    memcpy(table(pdpt), table(kernel_pdpt), 4 * sizeof(uint64_t));
    table(pdpt)[4] = pd | 7;
    table(pd)[0] = pt | 7;
    table(pt)[0] = code | 5;
    table(pt)[1] = data | 7 | NX;
    table(pt)[3] = stack | 7 | NX;
    return pml4;
}
/* Only private user tables are owned. Low kernel and direct-map tables are shared.
 * Caller must first switch away from this root; no task migrates between CPUs. */
void address_space_destroy(uint64_t root) {
    uint64_t pdpt = table(root)[0] & ADDRESS;
    uint64_t pd = table(pdpt)[4] & ADDRESS;
    uint64_t pt = table(pd)[0] & ADDRESS;
    page_free(pt); page_free(pd); page_free(pdpt); page_free(root);
}
