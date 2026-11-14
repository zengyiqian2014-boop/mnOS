/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "guest_memory.h"
#include <string.h>
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put64(uint8_t *p, uint64_t n) {
    for (unsigned i = 0; i < 8; ++i) p[7 - i] = (uint8_t)(n >> (i * 8));
}
bool guest_dtb_memory(void *blob, size_t length, uint64_t bytes) {
    uint8_t *b = blob;
    if (length < 40 || be32(b) != 0xd00dfeed || be32(b + 4) > length ||
        bytes < 65536 || bytes > (UINT64_C(1) << 39) || (bytes & 4095)) return false;
    size_t total = be32(b + 4), pos = be32(b + 8), strings = be32(b + 12);
    size_t ss = be32(b + 32), st = be32(b + 36);
    if (pos < 40 || strings < 40 || pos > total || st > total - pos || strings > total || ss > total - strings)
        return false;
    if ((pos & 3) || (st & 3) || !st || !ss ||
        !(pos + st <= strings || strings + ss <= pos)) return false;
    size_t end = pos + st, target = 0;
    unsigned depth = 0, memory_depth = 0;
    while (pos <= end && end - pos >= 4) {
        uint32_t tag = be32(b + pos); pos += 4;
        if (tag == 1) {
            size_t start = pos;
            while (pos < end && b[pos]) ++pos;
            if (pos == end || depth == 64) return false;
            ++depth;
            if (depth == 2 && pos - start == 15 && !memcmp(b + start, "memory@80000000", 15)) memory_depth = depth;
            pos = (pos + 4) & ~(size_t)3;
        } else if (tag == 2) {
            if (!depth) return false;
            if (depth == memory_depth) memory_depth = 0;
            --depth;
        } else if (tag == 3) {
            if (end - pos < 8 || !depth) return false;
            size_t size = be32(b + pos), name = be32(b + pos + 4); pos += 8;
            if (size > end - pos || name >= ss) return false;
            size_t terminator = name;
            while (terminator < ss && b[strings + terminator]) ++terminator;
            if (terminator == ss) return false;
            if (memory_depth == depth && !strcmp((char *)b + strings + name, "reg")) {
                if (target || size != 16 || be32(b + pos) || be32(b + pos + 4) != 0x80000000) return false;
                target = pos + 8;
            }
            pos += (size + 3) & ~(size_t)3;
        } else if (tag == 4) {
            continue;
        } else if (tag == 9) {
            if (depth || !target) return false;
            put64(b + target, bytes); return true;
        } else return false;
    }
    return false;
}
