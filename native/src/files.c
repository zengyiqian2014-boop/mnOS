/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "block.h"
#include "mnfs.h"
#include "auth.h"
#include "process.h"
#include "disk_layout.h"
extern char native_hello_start[], native_hello_end[], native_spin_start[], native_spin_end[];
struct chunk { size_t size; struct chunk *next; bool free; } __attribute__((aligned(16)));
static struct chunk *heap;
static struct mnfs ram, disk;
static struct block_device disks[4];
static uint8_t *ram_data;
static bool disk_present;
struct volume { struct block_device *device; uint64_t start, sectors; };
static struct volume mounted;
static bool raw_read(void *p,uint64_t a,unsigned n,void *b) {
    uint8_t *out=b;while(n){unsigned chunk=n>8?8:n;if(!block_read(p,a,chunk,out))return false;a+=chunk;n-=chunk;out+=chunk*512;}return true;
}
static bool disk_locked;
static void accounts_init(void);
#define RAM_BLOCKS 2048
static void *allocate(size_t bytes) {
    if (!bytes || bytes > SIZE_MAX - 15) return NULL;
    bytes = (bytes + 15) & ~(size_t)15;
    for (struct chunk *c = heap; c; c = c->next) if (c->free && c->size >= bytes) {
        if (c->size - bytes > sizeof *c + 16) {
            struct chunk *next = (void *)((uint8_t *)(c + 1) + bytes);
            *next = (struct chunk){c->size - bytes - sizeof *c, c->next, true};
            c->next = next; c->size = bytes;
        }
        c->free = false; return c + 1;
    }
    return NULL;
}
static void release(void *p) {
    if (!p) return;
    struct chunk *c = (struct chunk *)p - 1; c->free = true;
    for (c = heap; c && c->next;)
        if (c->free && c->next->free) { c->size += sizeof *c + c->next->size; c->next = c->next->next; }
        else c = c->next;
}
static bool ram_read(void *context, uint64_t block, void *p) {
    (void)context; if (block >= RAM_BLOCKS) return false;
    memcpy(p, ram_data + block * MNFS_BLOCK, MNFS_BLOCK); return true;
}
static bool ram_write(void *context, uint64_t block, const void *p) {
    (void)context; if (block >= RAM_BLOCKS) return false;
    memcpy(ram_data + block * MNFS_BLOCK, p, MNFS_BLOCK); return true;
}
static bool ram_flush(void *context) { (void)context; return true; }
static bool disk_read(void *context, uint64_t block, void *p) {
    struct volume *v = context;
    return block < v->sectors / 8 && block_read(v->device, v->start + block * 8, 8, p);
}
static bool disk_write(void *context, uint64_t block, const void *p) {
    struct volume *v = context;
    return block < v->sectors / 8 && block_write(v->device, v->start + block * 8, 8, p);
}
static bool disk_flush(void *context) { return block_flush(((struct volume *)context)->device); }
void files_init(void) {
    unsigned count = block_discover(disks, ARRAY_SIZE(disks));
    size_t pool = 8u << 20;
    for (unsigned i = 0; i < count; ++i) {
        uint64_t maps = (disks[i].sectors / 8 + 7) / 8 * 2;
        if (maps + (1u << 20) > pool && maps < pages_free() * PAGE / 4) pool = maps + (1u << 20);
    }
    heap = kernel_alloc(pool); *heap = (struct chunk){pool - sizeof *heap, NULL, true};
    ram_data = kernel_alloc(RAM_BLOCKS * MNFS_BLOCK);
    struct mnfs_io io = {NULL, RAM_BLOCKS, ram_read, ram_write, ram_flush, allocate, release};
    if (mnfs_format(&ram, &io) || mnfs_create(&ram, "/home", true) ||
        mnfs_create(&ram, "/home/readme.txt", false)) panic("RAM filesystem initialization");
    const char *readme = "mnOS MNFS v1\n/ram is temporary. /disk persists on an existing MNFS block image.\n"
        "64-bit sizes, sparse files, CoW B-tree, CRC32C, two checkpoints and one saved snapshot.\n";
    if (mnfs_write(&ram, "/home/readme.txt", 0, readme, strlen(readme))) panic("RAM filesystem write");
    if (mnfs_create(&ram, "/bin", true) || mnfs_create(&ram, "/bin/hello", false) ||
        mnfs_write(&ram, "/bin/hello", 0, native_hello_start, native_hello_end - native_hello_start) ||
        mnfs_create(&ram, "/bin/spin", false) ||
        mnfs_write(&ram, "/bin/spin", 0, native_spin_start, native_spin_end - native_spin_start))
        panic("native executable filesystem provisioning");
    char probe[16]; size_t got;
    if (mnfs_create(&ram, "/probe", false) || mnfs_write(&ram, "/probe", (1ULL << 32) - 2, "cross4g", 7) ||
        mnfs_write(&ram, "/probe", (1ULL << 45) + 5, "high", 4) ||
        mnfs_read(&ram, "/probe", (1ULL << 32) - 2, probe, 7, &got) || got != 7 || memcmp(probe, "cross4g", 7) ||
        mnfs_read(&ram, "/probe", (1ULL << 45) + 5, probe, 4, &got) || got != 4 || memcmp(probe, "high", 4) ||
        mnfs_remove(&ram, "/probe")) panic("filesystem large-offset self-test");
    log_write("FILESYSTEM PASS: MNFS CoW / CRC32C / offsets across 4 GiB and above 32 TiB\n");
    for (unsigned i = 0; i < count && !disk_present; ++i) {
        mounted = (struct volume){&disks[i], 0, disks[i].sectors};
        struct disk_io raw = {&disks[i], disks[i].sectors, raw_read, NULL, NULL};
        if (disk_data_partition(&raw, &mounted.start, &mounted.sectors))
            log_write("FILESYSTEM: installed GPT / MNFS partition found\n");
        io = (struct mnfs_io){&mounted, mounted.sectors / 8, disk_read, disk_write, disk_flush, allocate, release};
        int error = mnfs_mount(&disk, &io, false);
        if (!error) { disk_present = true; log_write("FILESYSTEM: /disk validated read-only; sign in for authorized writes\n"); }
    }
    if (!disk_present) log_write("FILESYSTEM: /ram ready; no mountable MNFS block disk\n");
    accounts_init();
}
static int make_directory(struct mnfs *f,const char *path) {
    struct mnfs_stat st;int e=mnfs_stat(f,path,&st);
    if(e==MN_NOENT)return mnfs_create(f,path,true);
    return e?e:st.directory?MN_OK:MN_NOTDIR;
}
static int homes(struct mnfs *f,const struct auth_database *db) {
    int e=make_directory(f,"/home");if(e)return e;
    e=make_directory(f,"/root");if(e)return e;
    for(unsigned i=1;i<db->count&&i<AUTH_USERS;++i) {
        char path[40]="/home/";size_t n=0;while(n<32&&db->users[i].name[n])++n;
        if(n==32)return MN_INVALID;
        memcpy(path+6,db->users[i].name,n+1);e=make_directory(f,path);if(e)return e;
    }
    return MN_OK;
}
static int accounts_load(struct auth_database *db) {
    if(!disk_present)return 1;
    struct mnfs_stat st;int e=mnfs_stat(&disk,"/system/accounts.bin",&st);
    if(e==MN_NOENT)return 1;
    if(e||st.directory||st.size!=sizeof *db)return -1;
    size_t got;e=mnfs_read(&disk,"/system/accounts.bin",0,db,sizeof *db,&got);
    return e||got!=sizeof *db?-1:0;
}
static int accounts_save(const struct auth_database *db) {
    if(homes(&ram,db))return -1;
    if(!disk_present)return 0;
    if(disk.poisoned)return -1;
    bool was=disk.writable;disk.writable=true;
    int e=homes(&disk,db);
    if(!e)e=make_directory(&disk,"/system");
    struct mnfs_stat st;
    if(!e) {e=mnfs_stat(&disk,"/system/accounts.bin",&st);if(e==MN_NOENT)e=mnfs_create(&disk,"/system/accounts.bin",false);}
    /* The fixed-size credential record fits in one CoW data block. */
    if(!e)e=mnfs_write(&disk,"/system/accounts.bin",0,db,sizeof *db);
    disk.writable=!disk.poisoned&&(was||!disk_locked);return e?-1:0;
}
static void accounts_init(void) {
    struct auth_io io={accounts_load,accounts_save,arch_random,disk_present};auth_init(&io);
}
void files_session(void) {
    if(!auth_logged_in())return;
    /* RAM is new at every boot. Provision only the authenticated user's home. */
    char path[40]="/home/";size_t n=strlen(auth_name());memcpy(path+6,auth_name(),n+1);
    make_directory(&ram,auth_admin()?"/root":path);
    if(disk_present&&!disk_locked&&!disk.poisoned)disk.writable=true;
}
struct mnfs *files_volume(const char **path) {
    const char *p = *path;
    if (strlen(p) >= 4 && !memcmp(p, "/ram", 4) && (!p[4] || p[4] == '/')) {
        *path = p[4] ? p + 4 : "/"; return &ram;
    }
    if (strlen(p) >= 5 && !memcmp(p, "/disk", 5) && (!p[5] || p[5] == '/')) {
        *path = p[5] ? p + 5 : "/"; return disk_present ? &disk : NULL;
    }
    return &ram;
}
static bool decimal(const char *p, uint64_t *out) {
    uint64_t value = 0;
    if (!*p) return false;
    while (*p) { if (*p < '0' || *p > '9' || value > (UINT64_MAX - (*p - '0')) / 10) return false;
        value = value * 10 + (*p++ - '0'); }
    *out = value; return true;
}
int files_settings(unsigned *width, unsigned *height, unsigned *wallpaper, bool save) {
    if(!auth_logged_in())return MN_RO;
    if (!disk_present) return MN_NOENT;
    char path[64]="/home/";
    if(auth_admin())memcpy(path,"/root/display.cfg",18);
    else {size_t n=strlen(auth_name());memcpy(path+6,auth_name(),n);memcpy(path+6+n,"/display.cfg",13);}
    if(!auth_path(path,save))return MN_RO;
    if (save) {
        if (!disk.writable) return MN_RO;
        char buffer[80], number_text[24]; size_t at = 0;
        unsigned values[] = {*width, *height, *wallpaper};
        for (unsigned i = 0; i < 3; ++i) {
            number(number_text, values[i]); size_t n = strlen(number_text);
            memcpy(buffer + at, number_text, n); at += n; buffer[at++] = i == 2 ? '\n' : ' ';
        }
        struct mnfs_stat st; int e = mnfs_stat(&disk, path, &st);
        if (e == MN_NOENT) e = mnfs_create(&disk, path, false);
        if (!e) e = mnfs_write(&disk, path, 0, buffer, at);
        if (!e) e = mnfs_truncate(&disk, path, at);
        return e;
    }
    char buffer[81]; size_t got;
    int e = mnfs_read(&disk, path, 0, buffer, sizeof buffer - 1, &got);
    if (e) return e;
    buffer[got] = 0; unsigned values[3]; char *p = buffer;
    for (unsigned i = 0; i < 3; ++i) {
        char *start = p; while (*p >= '0' && *p <= '9') ++p;
        if ((i < 2 && *p != ' ') || (i == 2 && *p != '\n')) return MN_INVALID;
        *p++ = 0; uint64_t n; if (!decimal(start, &n) || n > 65535) return MN_INVALID;
        values[i] = n;
    }
    if (*p || values[0] < 640 || values[0] > 2560 || values[1] < 480 || values[1] > 1440 || values[2] > 2) return MN_INVALID;
    *width = values[0]; *height = values[1]; *wallpaper = values[2]; return MN_OK;
}
static char *word(char **p) {
    while (**p == ' ') ++*p;
    char *start = *p;
    while (**p && **p != ' ') ++*p;
    if (**p) *(*p)++ = 0;
    return start;
}
static void (*output)(const char *);
static void emit(const char *s) { output(s); log_write("FS: "); log_write(s); log_write("\n"); }
static void value(const char *label, uint64_t n) {
    char line[100], number_text[24]; number(number_text, n);
    size_t a = strlen(label), b = strlen(number_text);
    if (a + b >= sizeof line) return;
    memcpy(line, label, a); memcpy(line + a, number_text, b + 1); emit(line);
}
static bool entry(void *unused, const char *name, const struct mnfs_stat *st) {
    (void)unused;
    char row[360], n[24]; number(n, st->size);
    size_t at = 0; const char *kind = st->directory ? "[DIR]  " : "[FILE] ";
    while (*kind) row[at++] = *kind++;
    while (*name && at < 270) row[at++] = *name++;
    row[at++] = ' '; for (unsigned i = 0; n[i]; ++i) row[at++] = n[i]; row[at] = 0;
    emit(row); return true;
}
bool files_command(const char *command, void (*print)(const char *)) {
    if (process_command(command, print)) return true;
    char buffer[256]; size_t length = strlen(command);
    if (length >= sizeof buffer) return false;
    memcpy(buffer, command, length + 1);
    char *cursor = buffer, *cmd = word(&cursor), *arg = word(&cursor);
    const char *p = *arg ? arg : "/ram";
    struct mnfs *f = files_volume(&p); int error = MN_OK;
    output = print;
    bool recognized=streq(cmd,"fs")||streq(cmd,"disk")||streq(cmd,"ls")||streq(cmd,"stat")||streq(cmd,"cat")||
        streq(cmd,"write")||streq(cmd,"writeat")||streq(cmd,"mkdir")||streq(cmd,"rm")||streq(cmd,"truncate")||
        streq(cmd,"mv")||streq(cmd,"snapshot")||streq(cmd,"gc")||streq(cmd,"exec");
    if(!recognized)return false;
    if(!auth_logged_in()) {emit("Permission denied: sign in first");return true;}
    if(streq(cmd,"disk")||streq(cmd,"snapshot")||streq(cmd,"gc")) {
        if(!auth_admin()) {emit("Permission denied: root required for volume administration");return true;}
    } else if(!streq(cmd,"fs")) {
        bool write=!(streq(cmd,"ls")||streq(cmd,"stat")||streq(cmd,"cat")||streq(cmd,"exec"));
        if(!auth_path(p,write)) {emit("Permission denied: outside your home or protected system data");return true;}
    }
    if (streq(cmd, "exec")) {
        if (!*arg || *word(&cursor)) { emit("Usage: exec PATH (native ELF; no arguments yet)"); return true; }
        struct mnfs_stat st;
        error = f ? mnfs_stat(f, p, &st) : MN_NOENT;
        if (!error && (st.directory || !st.size || st.size > EXEC_FILE_LIMIT)) error = MN_INVALID;
        if (!error) {
            void *bytes = allocate((size_t)st.size); size_t got = 0;
            if (!bytes) { emit("exec: insufficient memory"); return true; }
            error = mnfs_read(f, p, 0, bytes, (size_t)st.size, &got);
            if (!error && got != st.size) error = MN_IO;
            if (!error) {
                int result = process_start(bytes, got, arg);
                if (result) emit(result == -16 ? "exec: boot checks pending" :
                                 result == -8 ? "exec: invalid or unsupported ELF" :
                                 result == -13 ? "exec: permission denied" : "exec: insufficient memory");
            }
            release(bytes);
        }
    } else if (streq(cmd, "fs")) {
        emit("MNFS v1 / 64-bit files / CoW / CRC32C / saved snapshot");
        emit(disk_present ? "/ram temporary; /disk present" : "/ram temporary; /disk absent");
        value("RAM free blocks: ", ram.free_blocks);
        if (disk_present) { value("Disk free blocks: ", disk.free_blocks); emit(disk.writable ? "/disk writable" : "/disk read-only"); }
    } else if (streq(cmd, "disk")) {
        if (streq(arg, "unlock") && disk_present && !disk.poisoned) { disk_locked=false;disk.writable = true; emit("/disk writes enabled"); }
        else if (streq(arg, "lock") && disk_present) { disk_locked=true;disk.writable = false; emit("/disk read-only"); }
        else emit("disk unlock | disk lock; only a mounted MNFS image is writable");
    } else if (streq(cmd, "ls")) error = f ? mnfs_list(f, p, entry, NULL) : MN_NOENT;
    else if (streq(cmd, "stat")) {
        struct mnfs_stat st; error = f ? mnfs_stat(f, p, &st) : MN_NOENT;
        if (!error) { value("Size: ", st.size); value("Inode: ", st.inode); }
    } else if (streq(cmd, "cat")) {
        uint64_t offset = 0; char *start = word(&cursor);
        if (*start && !decimal(start, &offset)) error = MN_INVALID;
        char bytes[513]; size_t got = 0;
        if (!error) error = f ? mnfs_read(f, p, offset, bytes, 512, &got) : MN_NOENT;
        if (!error) {
            size_t at = 0;
            while (at < got) {
                char row[96]; unsigned n = 0;
                while (at < got && n < 95) { char c = bytes[at++]; if (c == '\n') break;
                    row[n++] = c >= 32 && c < 127 ? c : '.'; }
                row[n] = 0; emit(row);
            }
            if (!got) emit("(end of file)");
        }
    } else if (streq(cmd, "write") || streq(cmd, "writeat")) {
        uint64_t offset = 0;
        if (streq(cmd, "writeat") && !decimal(word(&cursor), &offset)) error = MN_INVALID;
        while (*cursor == ' ') ++cursor;
        if (!error && f) {
            struct mnfs_stat st; error = mnfs_stat(f, p, &st);
            if (error == MN_NOENT) error = mnfs_create(f, p, false);
            if (!error) error = mnfs_write(f, p, offset, cursor, strlen(cursor));
            if (!error && streq(cmd, "write")) error = mnfs_truncate(f, p, strlen(cursor));
        } else if (!f) error = MN_NOENT;
        if (!error) emit("written and flushed");
    } else if (streq(cmd, "mkdir")) error = f ? mnfs_create(f, p, true) : MN_NOENT;
    else if (streq(cmd, "rm")) error = f ? mnfs_remove(f, p) : MN_NOENT;
    else if (streq(cmd, "truncate")) {
        uint64_t size; error = !decimal(word(&cursor), &size) ? MN_INVALID :
            f ? mnfs_truncate(f, p, size) : MN_NOENT;
    } else if (streq(cmd, "mv")) {
        const char *dest = word(&cursor); struct mnfs *other = files_volume(&dest);
        if(!auth_path(dest,true)) {emit("Permission denied: destination is protected");return true;}
        error = !*dest || !f || f != other ? MN_INVALID : mnfs_rename(f, p, dest);
    } else if (streq(cmd, "snapshot")) {
        const char *target = word(&cursor); if (!*target) target = "/ram";
        f = files_volume(&target);
        unsigned action = streq(arg, "save") ? 0 : streq(arg, "restore") ? 1 : streq(arg, "drop") ? 2 : 3;
        error = f ? mnfs_snapshot(f, action) : MN_NOENT;
        if (!error) {emit("snapshot committed");if(action==1&&f==&disk) {accounts_init();emit("Sign in again after restoring account data");}}
    } else if (streq(cmd, "gc")) error = f ? mnfs_collect(f) : MN_NOENT;
    else return false;
    if (error) emit(mnfs_error(error));
    return true;
}
