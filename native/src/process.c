/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "exec.h"
#include "auth.h"
#include "process.h"
#ifdef __aarch64__
#include "../arch/arm64/platform.h"
#endif
static struct {
    uint64_t pid, next_pid, free_before;
    uint32_t uid;
    bool active, dying;
    char path[256];
} application;
static void report(const char *event, int status) {
    char line[120], n[24];
    size_t at = 0;
    const char *prefix = "PROCESS ";
    memcpy(line, prefix, strlen(prefix)); at = strlen(prefix);
    number(n, application.pid); memcpy(line + at, n, strlen(n)); at += strlen(n);
    line[at++] = ' ';
    memcpy(line + at, event, strlen(event)); at += strlen(event);
    memcpy(line + at, " status=", 8); at += 8;
    if (status < 0) { line[at++] = '-'; number(n, -(int64_t)status); }
    else number(n, (unsigned)status);
    memcpy(line + at, n, strlen(n) + 1);
    ui_program_output(line);
}
bool process_active(void) { return !cpu_this()->index && application.active; }
bool process_dying(void) { return process_active() && application.dying; }
int process_start(const void *image, size_t size, const char *path) {
    if (cpu_this()->index || !auth_logged_in()) return -13;
    if (!cpu_this()->completed || application.active) return -16;
    if (!size || size > EXEC_FILE_LIMIT || strlen(path) >= sizeof application.path) return -8;
    if (application.next_pid == UINT64_MAX) return -12;
    struct task next = {0};
    uint64_t before = pages_free(), entry = 0;
    int error = -12;
    next.code = page_try_alloc();
    next.data = page_try_alloc();
    next.stack = page_try_alloc();
    if (!next.code || !next.data || !next.stack) goto fail;
#ifdef __aarch64__
    const unsigned machine = 183;
#else
    const unsigned machine = 62;
#endif
    if (elf_load(image, size, machine, USER_BASE, phys_pointer(next.code),
                 phys_pointer(next.data), &entry)) { error = -8; goto fail; }
    next.cr3 = address_space_try(next.code, next.data, next.stack);
    if (!next.cr3) goto fail;
#ifdef __aarch64__
    arm_cache_code(next.code, PAGE);
    next.frame.pc = entry;
    next.frame.sp = USER_STACK + PAGE;
    next.frame.psr = 0;
#else
    next.frame = (struct irq_frame){.rip = entry, .cs = 0x1b, .ss = 0x23,
                                    .rflags = 0x202, .rsp = USER_STACK - 16};
#endif
    application.pid = ++application.next_pid;
    application.free_before = before;
    application.uid = auth_uid();
    application.active = true;
    application.dying = false;
    memcpy(application.path, path, strlen(path) + 1);
    next.ready = true;
    tasks[APP_TASK] = next;
    report("started", 0);
    return 0;
fail:
    if (next.cr3) address_space_destroy(next.cr3);
    if (next.code) page_free(next.code);
    if (next.data) page_free(next.data);
    if (next.stack) page_free(next.stack);
    return error;
}
void process_exit(int status, const char *reason) {
    if (!process_active() || application.dying) return;
    tasks[APP_TASK].ready = false;
    tasks[APP_TASK].wake_tick = 0;
    application.dying = true;
    report(reason, status);
}
void process_reap(void) {
    if (!process_dying() || current_task == APP_TASK) return;
    struct task *t = &tasks[APP_TASK];
    address_space_destroy(t->cr3);
    /* Wipe user payloads before returning them to the allocator. */
    memset(phys_pointer(t->code), 0, PAGE);
    memset(phys_pointer(t->data), 0, PAGE);
    memset(phys_pointer(t->stack), 0, PAGE);
    page_free(t->code); page_free(t->data); page_free(t->stack);
    memset(t, 0, sizeof *t);
    application.active = application.dying = false;
    log_write("PROCESS reaped; pages restored=");
    log_dec(pages_free() == application.free_before);
    log_write("\n");
}
void process_session_check(void) {
    if (process_active() && (!auth_logged_in() || auth_uid() != application.uid))
        process_exit(125, "session-ended");
}
void process_tick(void) {
    if (!process_active() || application.dying) return;
    struct task *t = &tasks[APP_TASK];
    if (t->wake_tick && t->wake_tick <= ticks) {
        t->wake_tick = 0; t->ready = true;
    }
}
int64_t process_syscall(uint64_t call, uint64_t arg, uint64_t length, bool *reschedule) {
    *reschedule = false;
    if (!process_active() || application.dying) { *reschedule = true; return -13; }
    switch (call) {
    case 0: return process_write(arg, length);
    case 1: *reschedule = true; return 0;
    case 2: return ticks;
    case 3: process_exit((int)(arg & 255), "exited"); *reschedule = true; return 0;
    case 5:
        if (!arg || arg > 1000) return -22;
        tasks[APP_TASK].wake_tick = ticks + arg;
        tasks[APP_TASK].ready = false;
        *reschedule = true; return 0;
    default: return -38;
    }
}
int64_t process_write(uint64_t ptr, uint64_t length) {
    if (!process_active() || application.dying) return -13;
    if (length > 256) return -22;
    if (!length) return 0;
    struct task *t = &tasks[APP_TASK];
    uint64_t pa = 0, offset = 0;
    const uint64_t va[] = {USER_BASE, USER_DATA,
#ifdef __aarch64__
        USER_STACK
#else
        USER_STACK - PAGE
#endif
    };
    const uint64_t physical[] = {t->code, t->data, t->stack};
    for (unsigned i = 0; i < 3; ++i)
        if (ptr >= va[i] && ptr - va[i] < PAGE && length <= PAGE - (ptr - va[i])) {
            pa = physical[i]; offset = ptr - va[i]; break;
        }
    if (!pa) return -14;
    const unsigned char *p = phys_pointer(pa + offset);
    char line[100]; size_t at = 0;
    for (uint64_t i = 0; i < length; ++i) {
        if (p[i] == '\n' || at == sizeof line - 1) {
            line[at] = 0; ui_program_output(line); at = 0;
            if (p[i] == '\n') continue;
        }
        line[at++] = p[i] >= 32 && p[i] < 127 ? (char)p[i] : '.';
    }
    if (at) { line[at] = 0; ui_program_output(line); }
    return (int64_t)length;
}
bool process_command(const char *cmd, void (*print)(const char *)) {
    bool status = streq(cmd, "ps");
    bool kill = !strncmp(cmd, "kill ", 5);
    if (!status && !kill) return false;
    if (!auth_logged_in()) { print("Permission denied: sign in first"); return true; }
    if (!process_active()) { print("No native application running"); return true; }
    if (status) {
        char line[80] = "PID ", n[24]; number(line + 4, application.pid);
        size_t len = strlen(line); memcpy(line + len, " UID ", 5); number(n, application.uid);
        memcpy(line + len + 5, n, strlen(n) + 1); print(line); print(application.path);
        return true;
    }
    uint64_t id = 0; const char *p = cmd + 5;
    if (!*p) { print("Usage: kill PID"); return true; }
    while (*p) {
        if (*p < '0' || *p > '9' || id > (UINT64_MAX - (*p - '0')) / 10) {
            print("Usage: kill PID"); return true;
        }
        id = id * 10 + (*p++ - '0');
    }
    if (id != application.pid) print("No such process");
    else if (auth_uid() != application.uid && !auth_admin()) print("Permission denied");
    else process_exit(130, "terminated");
    return true;
}
