/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_PHYS_H
#define MNOS_PHYS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHYS_PAGE 4096ULL
#define PHYS_CAP (1ULL << 46) /* 64 TiB; four-level, high-half direct map. */
#define PHYS_SECTION (1ULL << 30)
#define PHYS_SECTIONS (PHYS_CAP / PHYS_SECTION)
#define PHYS_RANGES 1024
#define PHYS_MAP_WORDS 4096

struct phys_range { uint64_t start, end; };
struct phys_maps { uint64_t *available[8], *owned[8]; };
struct phys_section {
    struct phys_maps *maps;
    uint32_t free_pages, cursor;
};
struct phys_allocator {
    struct phys_range ranges[PHYS_RANGES];
    size_t count;
    struct phys_section section[PHYS_SECTIONS];
    struct phys_maps initial;
    uint64_t initial_free[PHYS_MAP_WORDS], initial_owned[PHYS_MAP_WORDS];
    uint64_t limit, free_pages, total_pages, ready_free, metadata_pages;
    unsigned hint, meta_hint;
    void *(*map)(uint64_t, void *);
    void *context;
    bool prepared;
};
void phys_reset(struct phys_allocator *, uint64_t, void *(*)(uint64_t, void *), void *);
bool phys_add(struct phys_allocator *, uint64_t, uint64_t);
bool phys_reserve(struct phys_allocator *, uint64_t, uint64_t);
bool phys_prepare(struct phys_allocator *);
/* Half-open address bounds; zero means no page available. */
uint64_t phys_alloc(struct phys_allocator *, uint64_t, uint64_t);
bool phys_release(struct phys_allocator *, uint64_t);
#endif
