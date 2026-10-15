/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mnfs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BLOCKS 8192u
#define BYTES ((size_t)BLOCKS * MNFS_BLOCK)
struct disk {
    unsigned char *data, *stable;
    uint16_t dirty[BLOCKS];
    unsigned operations, fail_at;
    bool torn;
};
static bool read_block(void *ctx, uint64_t n, void *out) {
    struct disk *d = ctx; assert(n < BLOCKS); memcpy(out, d->data + n * MNFS_BLOCK, MNFS_BLOCK); return true;
}
static bool write_block(void *ctx, uint64_t n, const void *in) {
    struct disk *d = ctx; assert(n < BLOCKS);
    if (++d->operations == d->fail_at) {
        if (d->torn) {
            memcpy(d->data + n * MNFS_BLOCK, in, MNFS_BLOCK / 2);
            d->dirty[n] = MNFS_BLOCK / 2;
        }
        return false;
    }
    memcpy(d->data + n * MNFS_BLOCK, in, MNFS_BLOCK); d->dirty[n] = MNFS_BLOCK; return true;
}
static void persist(struct disk *d, unsigned parity) {
    if (!d->stable) return;
    /* Simulate cache writes reaching media in reverse order, or only a subset. */
    for (unsigned n = BLOCKS; n-- > 0;)
        if (d->dirty[n] && (parity > 1 || n % 2 == parity)) {
            memcpy(d->stable + n * MNFS_BLOCK, d->data + n * MNFS_BLOCK, d->dirty[n]);
            d->dirty[n] = 0;
        }
}
static bool flush(void *ctx) {
    struct disk *d = ctx; bool success = ++d->operations != d->fail_at;
    persist(d, success ? 2 : 0); return success;
}
static struct mnfs_io io(struct disk *d) {
    return (struct mnfs_io){d, BLOCKS, read_block, write_block, flush, malloc, free};
}
static void ok(int e) { if (e) { fprintf(stderr, "MNFS: %s\n", mnfs_error(e)); abort(); } }
static void expect(struct mnfs *f, const char *name, uint64_t offset, const void *value, size_t size) {
    unsigned char *out = malloc(size + 1); size_t got = 0;
    ok(mnfs_read(f, name, offset, out, size, &got)); assert(got == size && !memcmp(out, value, size)); free(out);
}
static bool count(void *context, const char *name, const struct mnfs_stat *st) {
    assert(name[0] && st->inode); ++*(unsigned *)context; return true;
}
static void crash_tests(bool cached) {
    struct disk d = {.data = calloc(1, BYTES), .stable = cached ? calloc(1, BYTES) : NULL};
    struct mnfs_io device = io(&d); struct mnfs f;
    ok(mnfs_format(&f, &device)); ok(mnfs_create(&f, "/file", false));
    ok(mnfs_write(&f, "/file", 0, "before", 6)); mnfs_close(&f);
    unsigned char *baseline = malloc(BYTES); memcpy(baseline, d.data, BYTES);
    ok(mnfs_mount(&f, &device, true)); d.operations = 0;
    ok(mnfs_write(&f, "/file", 0, "after!", 6));
    unsigned steps = d.operations; mnfs_close(&f);
    for (unsigned torn = 0; torn < 2; ++torn) for (unsigned fail = 1; fail <= steps; ++fail) {
        memcpy(d.data, baseline, BYTES); d.fail_at = 0;
        if (cached) memcpy(d.stable, baseline, BYTES);
        memset(d.dirty, 0, sizeof d.dirty);
        ok(mnfs_mount(&f, &device, true)); d.operations = 0; d.fail_at = fail; d.torn = torn;
        assert(mnfs_write(&f, "/file", 0, "after!", 6) == MN_IO); mnfs_close(&f);
        if (cached) {
            persist(&d, fail % 2); /* Some queued blocks can reach disk before power is lost. */
            memcpy(d.data, d.stable, BYTES);
            memset(d.dirty, 0, sizeof d.dirty);
        }
        d.fail_at = 0; ok(mnfs_mount(&f, &device, false));
        char out[6]; size_t n;
        ok(mnfs_read(&f, "/file", 0, out, sizeof out, &n));
        assert(n == 6 && (!memcmp(out, "before", 6) || !memcmp(out, "after!", 6)));
        mnfs_close(&f);
    }
    free(baseline); free(d.data); free(d.stable);
    printf("PASS MNFS: %u write/flush failure boundaries, torn writes, cache=%s\n", steps * 2, cached ? "reordered" : "write-through");
}
static void space_and_corruption(void) {
    struct disk d = {.data = calloc(1, BYTES)};
    struct mnfs_io device = io(&d); device.blocks = 96;
    struct mnfs f; ok(mnfs_format(&f, &device)); ok(mnfs_create(&f, "/file", false));
    char block[MNFS_BLOCK]; memset(block, 'A', sizeof block);
    ok(mnfs_write(&f, "/file", 0, block, sizeof block));
    ok(mnfs_snapshot(&f, 0));
    unsigned written = 1;
    for (; written < 1000; ++written) {
        int e = mnfs_write(&f, "/file", (uint64_t)written * MNFS_BLOCK, block, sizeof block);
        if (e == MN_NOSPC) break;
        ok(e);
    }
    assert(written < 1000);
    struct mnfs_stat st; ok(mnfs_stat(&f, "/file", &st));
    assert(st.size == (uint64_t)written * MNFS_BLOCK);
    mnfs_close(&f); ok(mnfs_mount(&f, &device, true));
    expect(&f, "/file", st.size - MNFS_BLOCK, block, sizeof block);
    /* Restoring an existing root needs no free data blocks, even at ENOSPC. */
    ok(mnfs_snapshot(&f, 1)); expect(&f, "/file", 0, block, sizeof block);
    ok(mnfs_collect(&f)); mnfs_close(&f);
    /* Corrupt the first file-data block. Data CRC must catch it, not return bytes. */
    unsigned corrupted = 0;
    for (unsigned n = 2; n < device.blocks; ++n)
        if (!memcmp(d.data + n * MNFS_BLOCK, block, sizeof block)) {
            d.data[n * MNFS_BLOCK + 200] ^= 1; ++corrupted;
        }
    assert(corrupted);
    ok(mnfs_mount(&f, &device, false)); size_t got;
    assert(mnfs_read(&f, "/file", 0, block, sizeof block, &got) == MN_CORRUPT);
    mnfs_close(&f); free(d.data);
    puts("PASS MNFS: ENOSPC rollback, snapshot at full disk, data corruption rejected");
}
static void checkpoint_recovery(void) {
    struct disk d = {.data = calloc(1, BYTES)}; struct mnfs_io device = io(&d); struct mnfs f;
    ok(mnfs_format(&f, &device)); ok(mnfs_create(&f, "/file", false));
    ok(mnfs_write(&f, "/file", 0, "before", 6));
    ok(mnfs_write(&f, "/file", 0, "after!", 6));
    uint64_t latest = f.root, previous = f.previous;
    assert(latest != previous); mnfs_close(&f);
    d.data[latest * MNFS_BLOCK + 200] ^= 1;
    ok(mnfs_mount(&f, &device, false)); expect(&f, "/file", 0, "before", 6); mnfs_close(&f);
    d.data[previous * MNFS_BLOCK + 200] ^= 1;
    assert(mnfs_mount(&f, &device, false) == MN_CORRUPT);
    free(d.data); puts("PASS MNFS: damaged latest metadata falls back; two damaged roots rejected");
}
int main(void) {
    struct disk d = {.data = calloc(1, BYTES)}; assert(d.data);
    struct mnfs_io device = io(&d); struct mnfs f; struct mnfs_stat st;
    ok(mnfs_format(&f, &device));
    ok(mnfs_create(&f, "/home", true)); ok(mnfs_create(&f, "/home/large", false));
    uint64_t edge = (1ULL << 32) - 3;
    ok(mnfs_write(&f, "/home/large", edge, "cross-4-GiB", 11));
    expect(&f, "/home/large", edge, "cross-4-GiB", 11);
    char zero[64] = {0}; expect(&f, "/home/large", 100000, zero, sizeof zero);
    ok(mnfs_write(&f, "/home/large", (1ULL << 45) + 37, "above-32-TiB", 12));
    expect(&f, "/home/large", (1ULL << 45) + 37, "above-32-TiB", 12);
    assert(mnfs_write(&f, "/home/large", UINT64_MAX, "x", 1) == MN_INVALID);
    ok(mnfs_stat(&f, "/home/large", &st)); assert(st.size == (1ULL << 45) + 49);
    ok(mnfs_snapshot(&f, 0));
    ok(mnfs_truncate(&f, "/home/large", 2)); ok(mnfs_truncate(&f, "/home/large", (1ULL << 33)));
    expect(&f, "/home/large", edge, zero, 11);
    ok(mnfs_snapshot(&f, 1)); expect(&f, "/home/large", edge, "cross-4-GiB", 11);
    for (unsigned i = 0; i < 300; ++i) {
        char name[64], content[64]; snprintf(name, sizeof name, "/home/n%04u", i);
        snprintf(content, sizeof content, "record-%u", i);
        ok(mnfs_create(&f, name, false)); ok(mnfs_write(&f, name, 0, content, strlen(content)));
    }
    unsigned total = 0; ok(mnfs_list(&f, "/home", count, &total)); assert(total == 301);
    ok(mnfs_snapshot(&f, 0));
    for (unsigned i = 0; i < 100; ++i) { char name[64]; snprintf(name, sizeof name, "/home/n%04u", i); ok(mnfs_remove(&f, name)); }
    ok(mnfs_collect(&f)); ok(mnfs_snapshot(&f, 1));
    for (unsigned i = 0; i < 300; ++i) {
        char name[64], content[64]; snprintf(name, sizeof name, "/home/n%04u", i);
        snprintf(content, sizeof content, "record-%u", i); expect(&f, name, 0, content, strlen(content));
    }
    ok(mnfs_create(&f, "/home/sub", true));
    assert(mnfs_rename(&f, "/home", "/home/sub/cycle") == MN_INVALID);
    assert(mnfs_remove(&f, "/home") == MN_NOTEMPTY);
    ok(mnfs_rename(&f, "/home/n0000", "/home/sub/moved"));
    expect(&f, "/home/sub/../sub/moved", 0, "record-0", 8);
    mnfs_close(&f); ok(mnfs_mount(&f, &device, false));
    expect(&f, "/home/large", edge, "cross-4-GiB", 11);
    assert(mnfs_write(&f, "/home/large", 0, "x", 1) == MN_RO);
    assert(mnfs_create(&f, "/readonly", false) == MN_RO);
    mnfs_close(&f);
    /* Every invalid image read stays inside the supplied block device. */
    uint8_t *backup = malloc(BYTES); memcpy(backup, d.data, BYTES);
    for (unsigned i = 0; i < 100; ++i) {
        memcpy(d.data, backup, BYTES);
        memset(d.data + (i % 2) * MNFS_BLOCK + i, 0xff, 32);
        int e = mnfs_mount(&f, &device, false);
        assert(e == MN_OK || e == MN_CORRUPT); if (!e) mnfs_close(&f);
    }
    free(backup); free(d.data);
    puts("PASS MNFS: 4 GiB boundary, 32 TiB sparse offsets, B-tree splits, directories, rename, GC, snapshot and remount");
    crash_tests(false); crash_tests(true); space_and_corruption(); checkpoint_recovery();
    return 0;
}
