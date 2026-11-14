/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest_memory.h"
#include <string.h>
static uint64_t allocate(struct guest_memory *m) {
    if (m->pages >= m->budget) { m->failed = true; return 0; }
    uint64_t p = m->allocate();
    if (!p) { m->failed = true; return 0; }
    memset(m->pointer(p), 0, 4096); ++m->pages; return p;
}
void *guest_memory_page(void *context, uint64_t offset) {
    struct guest_memory *m = context;
    if (offset >= m->bytes || offset >= (UINT64_C(1) << 39) || !m->allocate || !m->pointer || !m->release)
        return NULL;
    if (!m->root && !(m->root = allocate(m))) return NULL;
    uint64_t p = m->root;
    for (unsigned shift = 30;; shift -= 9) {
        uint64_t *table = m->pointer(p);
        unsigned index = (unsigned)((offset >> shift) & 511);
        if (!table[index] && !(table[index] = allocate(m))) return NULL;
        p = table[index];
        if (shift == 12) return m->pointer(p);
    }
}
static void clear_page(struct guest_memory *m, uint64_t p, unsigned level) {
    uint64_t *table = m->pointer(p);
    if (level) for (unsigned i = 0; i < 512; ++i)
        if (table[i]) clear_page(m, table[i], level - 1);
    /* Do not leave guest data in the host page allocator or across accounts. */
    volatile unsigned char *wipe = (volatile unsigned char *)table;
    for (unsigned i = 0; i < 4096; ++i) wipe[i] = 0;
    m->release(p); --m->pages;
}
void guest_memory_clear(struct guest_memory *m) {
    if (m->root) clear_page(m, m->root, 3);
    m->root = 0; m->pages = 0; m->failed = false;
}
