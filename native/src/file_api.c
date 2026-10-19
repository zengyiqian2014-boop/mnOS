/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "auth.h"
#include "mnfs.h"
#include "file_api.h"
#include "usercopy.h"

static int result(int error) {
    switch (error) {
    case MN_OK: return 0;
    case MN_NOMEM: return -12;
    case MN_NOSPC: return -28;
    case MN_NOENT: return -2;
    case MN_EXISTS: return -17;
    case MN_NOTDIR: return -20;
    case MN_ISDIR: return -21;
    case MN_NOTEMPTY: return -39;
    case MN_INVALID: return -22;
    case MN_RO: return -30;
    default: return -5; /* I/O or checksum corruption: no success substitution. */
    }
}
static int path_copy(const struct task *t, uint64_t ptr, char *path) {
    for (unsigned n = 0; n <= NATIVE_FILE_PATH; ++n) {
        if (ptr > UINT64_MAX - n) return -14;
        const char *p = user_buffer(t, ptr + n, 1, false);
        if (!p) return -14;
        path[n] = *p;
        if (!*p) return n && path[0] == '/' ? 0 : -22;
    }
    return -36;
}
static int64_t home(const struct task *t, const struct native_file_request *r) {
    if (r->path || r->offset > 1 || !r->length || r->length > NATIVE_FILE_TRANSFER) return -22;
    void *out = user_buffer(t, r->buffer, r->length, true);
    if (!out) return -14;
    const char *volume = r->offset ? "/disk" : "/ram", *lookup = volume;
    if (!files_volume(&lookup)) return -2;
    char path[64]; size_t n = strlen(volume);
    memcpy(path, volume, n);
    if (auth_admin()) { memcpy(path + n, "/root", 6); n += 5; }
    else {
        memcpy(path + n, "/home/", 6); n += 6;
        size_t k = strlen(auth_name());
        if (k >= sizeof path - n) return -36;
        memcpy(path + n, auth_name(), k + 1); n += k;
    }
    if (r->length <= n) return -28;
    memcpy(out, path, n + 1); return (int64_t)n;
}
int64_t file_request(const struct task *t, uint32_t uid, uint64_t ptr, uint64_t size) {
    /* App ownership is fixed at launch. A switched/locked console session must
     * not lend its privileges to a process awaiting timer-based revocation. */
    if (!auth_logged_in() || uid != auth_uid()) return -13;
    if (size != sizeof(struct native_file_request)) return -22;
    const void *input = user_buffer(t, ptr, size, false);
    if (!input) return -14;
    struct native_file_request r;
    memcpy(&r, input, sizeof r);
    if (r.operation > NATIVE_FILE_HOME) return -22;
    if (r.operation == NATIVE_FILE_HOME) return home(t, &r);

    char path[NATIVE_FILE_PATH + 1];
    int error = path_copy(t, r.path, path);
    if (error) return error;
    const char *p = path;
    struct mnfs *f = files_volume(&p);
    bool read = r.operation == NATIVE_FILE_READ || r.operation == NATIVE_FILE_STAT;
    if (!auth_path(p, !read)) return -13;
    if (!f) return -2;

    if (r.operation == NATIVE_FILE_READ || r.operation == NATIVE_FILE_WRITE) {
        if (r.length > NATIVE_FILE_TRANSFER || r.offset > MNFS_MAX_FILE ||
            r.length > MNFS_MAX_FILE - r.offset) return -22;
        void *data = r.length ? user_buffer(t, r.buffer, r.length, read) : NULL;
        if (r.length && !data) return -14;
        /* Copy all input before mutation; copy output only after a successful
         * checked read. MNFS errors cannot leak partially filled kernel data. */
        unsigned char bytes[NATIVE_FILE_TRANSFER]; size_t done = 0;
        if (read) {
            error = mnfs_read(f, p, r.offset, bytes, r.length, &done);
            if (!error && done) memcpy(data, bytes, done);
        } else {
            if (r.length) memcpy(bytes, data, r.length);
            error = mnfs_write(f, p, r.offset, bytes, r.length); done = r.length;
        }
        return error ? result(error) : (int64_t)done;
    }
    if (r.operation == NATIVE_FILE_STAT) {
        if (r.offset || r.length != sizeof(struct native_file_stat)) return -22;
        void *data = user_buffer(t, r.buffer, r.length, true);
        if (!data) return -14;
        struct mnfs_stat st; error = mnfs_stat(f, p, &st);
        if (error) return result(error);
        struct native_file_stat out = {st.size, st.inode, st.directory ? 1 : 0};
        memcpy(data, &out, sizeof out); return 0;
    }
    if (r.buffer || r.length || (r.operation != NATIVE_FILE_TRUNCATE && r.offset)) return -22;
    switch (r.operation) {
    case NATIVE_FILE_CREATE: error = mnfs_create(f, p, false); break;
    case NATIVE_FILE_MKDIR: error = mnfs_create(f, p, true); break;
    case NATIVE_FILE_REMOVE: error = mnfs_remove(f, p); break;
    case NATIVE_FILE_TRUNCATE:
        if (r.offset > MNFS_MAX_FILE) return -22;
        error = mnfs_truncate(f, p, r.offset); break;
    default: return -22;
    }
    return result(error);
}
