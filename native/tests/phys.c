/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "phys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct phys_allocator allocator;
static struct { uint64_t physical; void *data; } mapped[4096];
static size_t mapped_count;
static void *map(uint64_t p, void *context) {
    (void)context;
    assert(p && !(p % PHYS_PAGE));
    for (size_t i = 0; i < mapped_count; ++i)
        if (mapped[i].physical == p) return mapped[i].data;
    assert(mapped_count < sizeof mapped / sizeof mapped[0]);
    mapped[mapped_count].physical = p;
    mapped[mapped_count].data = calloc(1, PHYS_PAGE);
    assert(mapped[mapped_count].data);
    return mapped[mapped_count++].data;
}
static void reset(uint64_t limit) {
    for (size_t i = 0; i < mapped_count; ++i) free(mapped[i].data);
    mapped_count = 0;
    phys_reset(&allocator, limit, map, NULL);
}
static uint32_t random_state = 0x791149;
static uint32_t random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
int main(void) {
    /* Full 64 TiB accounting without allocating proportional host metadata. */
    reset(PHYS_CAP);
    assert(phys_add(&allocator, 0, PHYS_CAP));
    assert(phys_prepare(&allocator));
    assert(allocator.total_pages == PHYS_CAP / PHYS_PAGE - 1);
    uint64_t top = phys_alloc(&allocator, PHYS_CAP - PHYS_PAGE, PHYS_CAP);
    assert(top == PHYS_CAP - PHYS_PAGE);
    assert(allocator.metadata_pages == 17);
    assert(allocator.free_pages == allocator.total_pages - 18);
    assert(phys_release(&allocator, top));
    assert(!phys_release(&allocator, top));
    assert(!phys_release(&allocator, 0));
    for (size_t i = 0; i < mapped_count; ++i)
        assert(!phys_release(&allocator, mapped[i].physical));
    assert(!phys_add(&allocator, 0, PHYS_PAGE));
    assert(!phys_reserve(&allocator, 0, PHYS_PAGE));

    /* Several sparse NUMA-like ranges, a reserved hole, fragmented frees. */
    reset(PHYS_CAP);
    uint64_t high = (1ULL << 45) + PHYS_SECTION;
    assert(phys_add(&allocator, 0, 128ULL << 20));
    assert(phys_add(&allocator, high, 2 * PHYS_SECTION));
    assert(phys_add(&allocator, high + PHYS_PAGE, PHYS_SECTION)); /* overlapping duplicate */
    assert(phys_reserve(&allocator, high + PHYS_PAGE * 21, PHYS_PAGE * 19));
    assert(phys_reserve(&allocator, 0, 2ULL << 20));
    assert(phys_prepare(&allocator));
    uint64_t allocation[6000];
    for (unsigned i = 0; i < 6000; ++i) {
        allocation[i] = phys_alloc(&allocator, i & 1 ? high : 0, i & 1 ? PHYS_CAP : (1ULL << 32));
        assert(allocation[i]);
        assert(!(allocation[i] >= high + PHYS_PAGE * 21 && allocation[i] < high + PHYS_PAGE * 40));
        assert(!phys_release(&allocator, allocation[i] + 1));
        for (unsigned j = 0; j < i; ++j) assert(allocation[i] != allocation[j]);
    }
    for (unsigned i = 6000; i > 1; --i) {
        unsigned j = random32() % i;
        uint64_t swap = allocation[j]; allocation[j] = allocation[i - 1]; allocation[i - 1] = swap;
    }
    for (unsigned i = 0; i < 6000; ++i) {
        assert(phys_release(&allocator, allocation[i]));
        assert(!phys_release(&allocator, allocation[i]));
    }
    assert(allocator.free_pages + allocator.metadata_pages == allocator.total_pages);

    /* Differential map normalization and exhaustion, including unordered input. */
    for (unsigned round = 0; round < 40; ++round) {
        reset(1ULL << 36);
        bool expected[4096] = {0}, seen[4096] = {0};
        for (unsigned i = 0; i < 100; ++i) {
            unsigned start = random32() % 4096, end = start + random32() % (4097 - start);
            assert(phys_add(&allocator, start * PHYS_PAGE, (end - start) * PHYS_PAGE));
            for (unsigned j = start; j < end; ++j) expected[j] = true;
        }
        for (unsigned i = 0; i < 50; ++i) {
            unsigned start = random32() % 4096, end = start + random32() % (4097 - start);
            assert(phys_reserve(&allocator, start * PHYS_PAGE, (end - start) * PHYS_PAGE));
            for (unsigned j = start; j < end; ++j) expected[j] = false;
        }
        expected[0] = false;
        unsigned count = 0;
        for (unsigned i = 0; i < 4096; ++i) count += expected[i];
        assert(phys_prepare(&allocator) == (count != 0));
        assert(allocator.total_pages == count);
        unsigned taken = 0;
        uint64_t p;
        while ((p = phys_alloc(&allocator, 0, PHYS_CAP))) {
            assert(p / PHYS_PAGE < 4096 && expected[p / PHYS_PAGE] && !seen[p / PHYS_PAGE]);
            seen[p / PHYS_PAGE] = true; ++taken;
        }
        assert(taken == count);
        for (unsigned i = 1; i < 4096; ++i) assert(phys_release(&allocator, i * PHYS_PAGE) == expected[i]);
    }
    reset(1ULL << 36);
    assert(phys_add(&allocator, (1ULL << 36) - PHYS_PAGE, UINT64_MAX));
    assert(phys_add(&allocator, UINT64_MAX - 10, UINT64_MAX));
    assert(phys_reserve(&allocator, UINT64_MAX - 10, UINT64_MAX));
    assert(phys_prepare(&allocator));
    assert(allocator.total_pages == 1);
    assert(phys_alloc(&allocator, 0, PHYS_CAP) == (1ULL << 36) - PHYS_PAGE);
    assert(!phys_alloc(&allocator, 0, PHYS_CAP));
    reset(PHYS_CAP);
    puts("PASS physical allocator: 64 TiB, sparse holes, ownership, overflow, randomized maps and exhaustion");
}
