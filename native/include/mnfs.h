/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_FS_H
#define MNOS_FS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MNFS_BLOCK 4096u
#define MNFS_NAME 255u
#define MNFS_PATH 1024u
#define MNFS_MAX_FILE INT64_MAX
enum mnfs_error { MN_OK, MN_IO, MN_CORRUPT, MN_NOMEM, MN_NOSPC, MN_NOENT,
                 MN_EXISTS, MN_NOTDIR, MN_ISDIR, MN_NOTEMPTY, MN_INVALID, MN_RO };
struct mnfs_io {
    void *context;
    uint64_t blocks;
    bool (*read)(void *, uint64_t, void *);
    bool (*write)(void *, uint64_t, const void *);
    bool (*flush)(void *);
    void *(*allocate)(size_t);
    void (*release)(void *);
};
struct mnfs {
    struct mnfs_io io;
    uint64_t root, generation, next_inode, snapshot, previous, previous_snapshot;
    uint64_t free_blocks, cursor;
    uint8_t *used, *seen;
    unsigned slot;
    bool writable, poisoned, collect;
};
struct mnfs_stat { uint64_t inode, size; bool directory; };
typedef bool (*mnfs_visit)(void *, const char *, const struct mnfs_stat *);
int mnfs_format(struct mnfs *, const struct mnfs_io *);
int mnfs_mount(struct mnfs *, const struct mnfs_io *, bool writable);
void mnfs_close(struct mnfs *);
int mnfs_stat(struct mnfs *, const char *, struct mnfs_stat *);
int mnfs_create(struct mnfs *, const char *, bool directory);
int mnfs_list(struct mnfs *, const char *, mnfs_visit, void *);
int mnfs_read(struct mnfs *, const char *, uint64_t, void *, size_t, size_t *);
int mnfs_write(struct mnfs *, const char *, uint64_t, const void *, size_t);
int mnfs_truncate(struct mnfs *, const char *, uint64_t);
int mnfs_remove(struct mnfs *, const char *);
int mnfs_rename(struct mnfs *, const char *, const char *);
int mnfs_snapshot(struct mnfs *, unsigned); /* 0 save, 1 restore, 2 drop. */
int mnfs_collect(struct mnfs *);
const char *mnfs_error(int);
#endif
