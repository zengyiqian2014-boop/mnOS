/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host utility for new MNFS images. Never opens a block device or follows a symlink.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "mnfs.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool read_block(void *ctx, uint64_t block, void *out) {
    return pread(*(int *)ctx, out, MNFS_BLOCK, (off_t)(block * MNFS_BLOCK)) == MNFS_BLOCK;
}
static bool write_block(void *ctx, uint64_t block, const void *in) {
    return pwrite(*(int *)ctx, in, MNFS_BLOCK, (off_t)(block * MNFS_BLOCK)) == MNFS_BLOCK;
}
static bool flush(void *ctx) { return fsync(*(int *)ctx) == 0; }
static bool list(void *unused, const char *name, const struct mnfs_stat *st) {
    (void)unused; printf("%c %12" PRIu64 " %s\n", st->directory ? 'd' : '-', st->size, name); return true;
}
static bool number(const char *s, uint64_t *n) {
    if (!s || !*s) return false;
    *n = 0;
    while (*s) { if (*s < '0' || *s > '9' || *n > (UINT64_MAX - (*s - '0')) / 10) return false;
        *n = *n * 10 + (*s++ - '0'); }
    return true;
}
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: mnfs format NEW_IMAGE SIZE_MiB\n"
            "       mnfs IMAGE ls|check|cat|stat|mkdir|create|rm|write|writeat|truncate|mv|snapshot [PATH] [VALUE]\n"
            "       mnfs IMAGE import NEW_PATH HOST_FILE (creates, never replaces)\n"
            "Examples: mnfs disk.img writeat /large 4294967294 cross4g\n"
            "          mnfs disk.img snapshot save\n");
        return 2;
    }
    bool format = !strcmp(argv[1], "format");
    const char *command = format ? "format" : argv[2], *image = format ? argv[2] : argv[1];
    bool writable = format || (strcmp(command, "ls") && strcmp(command, "cat") &&
                               strcmp(command, "stat") && strcmp(command, "check"));
    uint64_t bytes = 0;
    if (format && (argc != 4 || !number(argv[3], &bytes) || bytes < 1 || bytes > INT64_MAX / (1u << 20))) return 2;
    if (format) bytes *= 1u << 20;
    int fd = open(image, (writable ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC |
                         (format ? O_CREAT | O_EXCL : 0), 0600);
    if (fd < 0) { perror(image); return 1; }
    struct stat status;
    if (fstat(fd, &status) || !S_ISREG(status.st_mode) || (format && ftruncate(fd, (off_t)bytes))) {
        fprintf(stderr, "Expected a regular image file; existing files are never formatted.\n"); close(fd); return 1;
    }
    if (!format) {
        if (status.st_size < 32 * MNFS_BLOCK || status.st_size % MNFS_BLOCK) { close(fd); return 2; }
        bytes = (uint64_t)status.st_size;
    }
    struct mnfs_io io = {&fd, bytes / MNFS_BLOCK, read_block, write_block, flush, malloc, free};
    struct mnfs f;
    int e = format ? mnfs_format(&f, &io) : mnfs_mount(&f, &io, writable);
    const char *p = argc > 3 ? argv[3] : "/";
    if (!e && !format) {
        if (!strcmp(command, "check")) printf("MNFS v1: generation=%" PRIu64 " free_blocks=%" PRIu64 "\n", f.generation, f.free_blocks);
        else if (!strcmp(command, "ls")) e = mnfs_list(&f, p, list, NULL);
        else if (!strcmp(command, "stat")) {
            struct mnfs_stat st; e = mnfs_stat(&f, p, &st);
            if (!e) printf("inode=%" PRIu64 " size=%" PRIu64 " directory=%u\n", st.inode, st.size, st.directory);
        } else if (!strcmp(command, "cat")) {
            uint8_t buffer[65536]; size_t got; uint64_t offset = 0;
            if (argc > 4 && !number(argv[4], &offset)) e = MN_INVALID;
            while (!e) {
                e = mnfs_read(&f, p, offset, buffer, sizeof buffer, &got);
                if (e || !got) break;
                if (fwrite(buffer, 1, got, stdout) != got) { e = MN_IO; break; }
                offset += got;
            }
        } else if (!strcmp(command, "import") && argc == 5) {
            int source = open(argv[4], O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            struct stat st;
            if (source < 0 || fstat(source, &st) || !S_ISREG(st.st_mode)) e = MN_INVALID;
            else {
                e = mnfs_create(&f, p, false);
                if (!e) {
                    unsigned char buffer[65536]; uint64_t offset = 0;
                    for (;;) {
                        ssize_t got = read(source, buffer, sizeof buffer);
                        if (got < 0 && errno == EINTR) continue;
                        if (got < 0) { e = MN_IO; break; }
                        if (!got) break;
                        e = mnfs_write(&f, p, offset, buffer, (size_t)got);
                        if (e) break;
                        offset += (size_t)got;
                    }
                    if (e) (void)mnfs_remove(&f, p);
                }
            }
            if (source >= 0) close(source);
        } else if (!strcmp(command, "mkdir") || !strcmp(command, "create")) e = mnfs_create(&f, p, command[0] == 'm');
        else if (!strcmp(command, "rm")) e = mnfs_remove(&f, p);
        else if (!strcmp(command, "mv") && argc == 5) e = mnfs_rename(&f, p, argv[4]);
        else if (!strcmp(command, "write") && argc == 5) {
            e = mnfs_write(&f, p, 0, argv[4], strlen(argv[4]));
        } else if (!strcmp(command, "writeat") && argc == 6) {
            uint64_t offset;
            e = number(argv[4], &offset) ? mnfs_write(&f, p, offset, argv[5], strlen(argv[5])) : MN_INVALID;
        } else if (!strcmp(command, "truncate") && argc == 5) {
            uint64_t size; e = number(argv[4], &size) ? mnfs_truncate(&f, p, size) : MN_INVALID;
        } else if (!strcmp(command, "snapshot") && argc == 4)
            e = mnfs_snapshot(&f, !strcmp(p, "save") ? 0 : !strcmp(p, "restore") ? 1 : !strcmp(p, "drop") ? 2 : 3);
        else e = MN_INVALID;
    }
    mnfs_close(&f); close(fd);
    if (e) { fprintf(stderr, "%s\n", mnfs_error(e)); return 1; }
    return 0;
}
