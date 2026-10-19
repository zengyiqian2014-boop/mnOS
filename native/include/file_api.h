/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native ABI, shared by the freestanding kernel and applications. */
#ifndef MNOS_FILE_API_H
#define MNOS_FILE_API_H
#include <stdint.h>
#define NATIVE_FILE_CALL 8u
#define NATIVE_FILE_TRANSFER 4096u
#define NATIVE_FILE_PATH 1024u
enum native_file_operation {
    NATIVE_FILE_READ, NATIVE_FILE_WRITE, NATIVE_FILE_CREATE,
    NATIVE_FILE_MKDIR, NATIVE_FILE_REMOVE, NATIVE_FILE_TRUNCATE,
    NATIVE_FILE_STAT, NATIVE_FILE_HOME
};
/* Absolute user addresses, never kernel pointers. Offset is a byte offset for
 * read/write, new size for truncate, or volume selector (0 RAM, 1 disk) for home.
 * Unused fields must be zero. Each request/data buffer must fit one user page. */
struct native_file_request {
    uint64_t operation, path, buffer, length, offset;
};
struct native_file_stat { uint64_t size, inode, directory; };
_Static_assert(sizeof(struct native_file_request) == 40, "native file request ABI");
_Static_assert(sizeof(struct native_file_stat) == 24, "native file stat ABI");
#endif
