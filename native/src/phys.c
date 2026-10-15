/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "phys.h"
void *memset(void *, int, size_t);

static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint64_t end_at(struct phys_allocator *m, uint64_t start, uint64_t size) {
    return size > m->limit - start ? m->limit : start + size;
}
void phys_reset(struct phys_allocator *m, uint64_t limit,
                void *(*map)(uint64_t, void *), void *context) {
    memset(m, 0, sizeof *m);
    m->limit = min64(limit, PHYS_CAP) & ~(PHYS_PAGE - 1);
    m->map = map;
    m->context = context;
}
bool phys_add(struct phys_allocator *m, uint64_t start, uint64_t size) {
    if (m->prepared) return false;
    if (!size || start >= m->limit) return true;
    uint64_t end = end_at(m, start, size) & ~(PHYS_PAGE - 1);
    start = (start + PHYS_PAGE - 1) & ~(PHYS_PAGE - 1);
    if (start >= end) return true;
    size_t first = 0, last;
    while (first < m->count && m->ranges[first].end < start) ++first;
    last = first;
    while (last < m->count && m->ranges[last].start <= end) {
        start = min64(start, m->ranges[last].start);
        end = max64(end, m->ranges[last].end);
        ++last;
    }
    if (first == last) {
        if (m->count == PHYS_RANGES) return false;
        for (size_t i = m->count; i > first; --i) m->ranges[i] = m->ranges[i - 1];
        ++m->count;
    } else {
        for (size_t i = last; i < m->count; ++i) m->ranges[first + 1 + i - last] = m->ranges[i];
        m->count -= last - first - 1;
    }
    m->ranges[first] = (struct phys_range){start, end};
    return true;
}
bool phys_reserve(struct phys_allocator *m, uint64_t start, uint64_t size) {
    if (m->prepared) return false;
    if (!size || start >= m->limit) return true;
    uint64_t end = end_at(m, start, size);
    end = (end + PHYS_PAGE - 1) & ~(PHYS_PAGE - 1);
    start &= ~(PHYS_PAGE - 1);
    for (size_t i = 0; i < m->count;) {
        struct phys_range r = m->ranges[i];
        if (end <= r.start) break;
        if (start >= r.end) { ++i; continue; }
        if (start > r.start && end < r.end) {
            if (m->count == PHYS_RANGES) return false;
            for (size_t j = m->count; j > i + 1; --j) m->ranges[j] = m->ranges[j - 1];
            m->ranges[i].end = start;
            m->ranges[i + 1] = (struct phys_range){end, r.end};
            ++m->count;
            break;
        }
        if (start <= r.start && end >= r.end) {
            for (size_t j = i + 1; j < m->count; ++j) m->ranges[j - 1] = m->ranges[j];
            --m->count;
        } else {
            if (start <= r.start) m->ranges[i].start = end;
            else m->ranges[i].end = start;
            ++i;
        }
    }
    return true;
}
static uint64_t *word(struct phys_maps *maps, bool owned, unsigned index) {
    return (owned ? maps->owned[index / 512] : maps->available[index / 512]) + index % 512;
}
static void populate(struct phys_allocator *m, unsigned id, struct phys_maps *maps) {
    uint64_t base = (uint64_t)id * PHYS_SECTION, end = base + PHYS_SECTION;
    for (size_t i = 0; i < m->count; ++i) {
        uint64_t lo = max64(m->ranges[i].start, base), hi = min64(m->ranges[i].end, end);
        if (lo >= hi) continue;
        unsigned first = (lo - base) / PHYS_PAGE, last = (hi - base) / PHYS_PAGE;
        for (unsigned w = first / 64; w <= (last - 1) / 64; ++w) {
            unsigned l = first > w * 64 ? first - w * 64 : 0;
            unsigned h = last < (w + 1) * 64 ? last - w * 64 : 64;
            uint64_t mask = (UINT64_MAX << l) & (UINT64_MAX >> (64 - h));
            *word(maps, false, w) |= mask;
        }
    }
    m->section[id].maps = maps;
    m->ready_free += m->section[id].free_pages;
}
bool phys_prepare(struct phys_allocator *m) {
    if (m->prepared || !m->map || !phys_reserve(m, 0, PHYS_PAGE)) return false;
    for (size_t i = 0; i < m->count; ++i) {
        uint64_t start = m->ranges[i].start, end = m->ranges[i].end;
        for (unsigned s = start / PHYS_SECTION; s <= (end - 1) / PHYS_SECTION; ++s) {
            uint64_t lo = max64(start, (uint64_t)s * PHYS_SECTION);
            uint64_t hi = min64(end, ((uint64_t)s + 1) * PHYS_SECTION);
            m->section[s].free_pages += (hi - lo) / PHYS_PAGE;
        }
        m->free_pages += (end - start) / PHYS_PAGE;
    }
    m->total_pages = m->free_pages;
    unsigned first = 0;
    while (first < PHYS_SECTIONS && !m->section[first].free_pages) ++first;
    if (first == PHYS_SECTIONS) return false;
    for (unsigned i = 0; i < 8; ++i) {
        m->initial.available[i] = &m->initial_free[i * 512];
        m->initial.owned[i] = &m->initial_owned[i * 512];
    }
    populate(m, first, &m->initial);
    m->hint = m->meta_hint = first;
    m->prepared = true;
    return true;
}
static uint64_t take(struct phys_allocator *m, unsigned id, uint64_t min, uint64_t max,
                     bool owned) {
    struct phys_section *s = &m->section[id];
    if (!s->maps || !s->free_pages) return 0;
    uint64_t base = (uint64_t)id * PHYS_SECTION;
    unsigned lo = (max64(min, base) - base) / PHYS_PAGE;
    unsigned hi = (min64(max, base + PHYS_SECTION) - base) / PHYS_PAGE;
    if (lo >= hi) return 0;
    unsigned first = lo / 64, last = (hi - 1) / 64;
    unsigned begin = s->cursor >= first && s->cursor <= last ? s->cursor : first;
    for (unsigned n = 0; n <= last - first; ++n) {
        unsigned w = first + (begin - first + n) % (last - first + 1);
        uint64_t mask = *word(s->maps, false, w);
        if (w == first) mask &= UINT64_MAX << (lo % 64);
        if (w == last && hi % 64) mask &= UINT64_MAX >> (64 - hi % 64);
        if (!mask) continue;
        unsigned bit = (unsigned)__builtin_ctzll(mask);
        *word(s->maps, false, w) &= ~(1ULL << bit);
        if (owned) *word(s->maps, true, w) |= 1ULL << bit;
        --s->free_pages;
        --m->ready_free;
        --m->free_pages;
        s->cursor = w;
        return base + ((uint64_t)w * 64 + bit) * PHYS_PAGE;
    }
    return 0;
}
static uint64_t metadata(struct phys_allocator *m) {
    for (unsigned n = 0; n < PHYS_SECTIONS; ++n) {
        unsigned s = (m->meta_hint + n) % PHYS_SECTIONS;
        uint64_t p = take(m, s, 0, m->limit, false);
        if (p) { m->meta_hint = s; ++m->metadata_pages; return p; }
    }
    return 0;
}
static bool materialize(struct phys_allocator *m, unsigned id) {
    if (m->section[id].maps) return true;
    if (m->ready_free < 17) return false;
    uint64_t p = metadata(m);
    struct phys_maps *maps = m->map(p, m->context);
    memset(maps, 0, PHYS_PAGE);
    for (unsigned i = 0; i < 8; ++i) {
        maps->available[i] = m->map(metadata(m), m->context);
        maps->owned[i] = m->map(metadata(m), m->context);
        memset(maps->available[i], 0, PHYS_PAGE);
        memset(maps->owned[i], 0, PHYS_PAGE);
    }
    populate(m, id, maps);
    return true;
}
uint64_t phys_alloc(struct phys_allocator *m, uint64_t min, uint64_t max) {
    if (!m->prepared || min >= m->limit || min >= max) return 0;
    min = (min + PHYS_PAGE - 1) & ~(PHYS_PAGE - 1);
    max = min64(max, m->limit) & ~(PHYS_PAGE - 1);
    if (min >= max) return 0;
    unsigned first = min / PHYS_SECTION, last = (max - 1) / PHYS_SECTION;
    /* Preserve a metadata reserve inside the requested allocation zone. */
    if (m->ready_free <= 34 && m->ready_free >= 17) {
        unsigned best = PHYS_SECTIONS;
        for (unsigned i = first; i <= last; ++i)
            if (!m->section[i].maps && m->section[i].free_pages >= 18 &&
                (best == PHYS_SECTIONS || m->section[i].free_pages > m->section[best].free_pages)) best = i;
        if (best != PHYS_SECTIONS) materialize(m, best);
    }
    unsigned begin = m->hint >= first && m->hint <= last ? m->hint : first;
    for (unsigned n = 0; n <= last - first; ++n) {
        unsigned s = first + (begin - first + n) % (last - first + 1);
        if (!m->section[s].free_pages || !materialize(m, s)) continue;
        uint64_t p = take(m, s, min, max, true);
        if (p) { m->hint = s; return p; }
    }
    return 0;
}
bool phys_release(struct phys_allocator *m, uint64_t p) {
    if (!m->prepared || p >= m->limit || p % PHYS_PAGE) return false;
    unsigned id = p / PHYS_SECTION, index = (p % PHYS_SECTION) / PHYS_PAGE;
    struct phys_section *s = &m->section[id];
    if (!s->maps) return false;
    uint64_t mask = 1ULL << (index % 64), *owner = word(s->maps, true, index / 64);
    if (!(*owner & mask)) return false;
    *owner &= ~mask;
    *word(s->maps, false, index / 64) |= mask;
    ++s->free_pages;
    ++m->ready_free;
    ++m->free_pages;
    return true;
}
