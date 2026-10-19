/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "exec.h"
#include "auth.h"
#include "process.h"
#include "usercopy.h"
#include "file_api.h"
#ifdef __aarch64__
#include "../arch/arm64/platform.h"
#endif

/* Mutations run on the boot CPU with IRQs masked. No application migration.
 * Descriptors are allocated from RAM; 64 pages are reserved for kernel work.
 * Ready queue selection/removal is O(1). Deadline-ordered sleepers mean timer
 * ticks visit only tasks whose deadlines have elapsed. */
enum state { RUNNABLE, SLEEPING, DYING };
struct application {
    struct task task;
    struct application *prev, *next, *qprev, *qnext, *retired_next;
    uint64_t descriptor, pid, run_ticks, syscalls;
    uint32_t uid;
    enum state state;
    char path[256];
};
_Static_assert(sizeof(struct application) <= PAGE, "process descriptor fits one page");
static struct application *first, *last, *ready, *sleepers, *retired, *running;
static uint64_t next_pid;
static uint32_t observed_uid = UINT32_MAX;
#define PROCESS_PAGES 8u /* descriptor, code/data/stack, four private tables */
#define KERNEL_RESERVE_PAGES 64u
static struct application *current(void) {
    return !cpu_this()->index && current_task == APP_TASK ? running : NULL;
}
struct task *process_task(void) {
    struct application *p = current();
    return p ? &p->task : NULL;
}
bool process_active(void) { return !cpu_this()->index && first; }
bool process_dying(void) {
    struct application *p = current();
    return p && p->state == DYING;
}
bool process_sleeping(void) { return !cpu_this()->index && sleepers; }
static void ready_add(struct application *p) {
    if (!ready) { p->qprev = p->qnext = p; ready = p; }
    else {
        p->qprev = ready->qprev; p->qnext = ready;
        ready->qprev->qnext = p; ready->qprev = p;
    }
    p->state = RUNNABLE;
}
static void ready_remove(struct application *p) {
    if (p->qnext == p) ready = NULL;
    else {
        p->qprev->qnext = p->qnext; p->qnext->qprev = p->qprev;
        if (ready == p) ready = p->qnext;
    }
    p->qnext = p->qprev = NULL;
}
static void sleep_remove(struct application *p) {
    if (p->qprev) p->qprev->qnext = p->qnext;
    else sleepers = p->qnext;
    if (p->qnext) p->qnext->qprev = p->qprev;
    p->qnext = p->qprev = NULL;
}
struct task *process_choose(void) {
    if (cpu_this()->index || !ready) return NULL;
    running = ready; ready = ready->qnext;
    return &running->task;
}
static void report(struct application *p, const char *event, int status) {
    char line[120], n[24]; size_t at;
    const char *prefix = "PROCESS ";
    memcpy(line, prefix, strlen(prefix)); at = strlen(prefix);
    number(n, p->pid); memcpy(line + at, n, strlen(n)); at += strlen(n);
    line[at++] = ' ';
    memcpy(line + at, event, strlen(event)); at += strlen(event);
    memcpy(line + at, " status=", 8); at += 8;
    if (status < 0) { line[at++] = '-'; number(n, -(int64_t)status); }
    else number(n, (unsigned)status);
    memcpy(line + at, n, strlen(n) + 1); ui_program_output(line);
}
static void dispose(struct task *t) {
    if (t->cr3) address_space_destroy(t->cr3);
    const uint64_t pages[] = {t->code, t->data, t->stack};
    for (unsigned i = 0; i < ARRAY_SIZE(pages); ++i) if (pages[i]) {
        memset(phys_pointer(pages[i]), 0, PAGE); page_free(pages[i]);
    }
}
int process_start(const void *image, size_t size, const char *path) {
    if (cpu_this()->index || !auth_logged_in()) return -13;
    if (!cpu_this()->completed) return -16;
    process_session_check();
    if (!image || !path || !size || size > EXEC_FILE_LIMIT ||
        strlen(path) >= sizeof(((struct application *)0)->path)) return -8;
    if (next_pid == INT64_MAX || pages_free() < KERNEL_RESERVE_PAGES + PROCESS_PAGES)
        return -12;
    uint64_t descriptor = page_try_alloc();
    if (!descriptor) return -12;
    struct application *p = phys_pointer(descriptor);
    struct task *t = &p->task;
    uint64_t entry = 0; int error = -12;
    t->code = page_try_alloc(); t->data = page_try_alloc(); t->stack = page_try_alloc();
    if (!t->code || !t->data || !t->stack) goto fail;
#ifdef __aarch64__
    const unsigned machine = 183;
#else
    const unsigned machine = 62;
#endif
    if (elf_load(image, size, machine, USER_BASE, phys_pointer(t->code),
                 phys_pointer(t->data), &entry)) { error = -8; goto fail; }
    t->cr3 = address_space_try(t->code, t->data, t->stack);
    if (!t->cr3) goto fail;
#ifdef __aarch64__
    arm_cache_code(t->code, PAGE);
    t->frame.pc = entry; t->frame.sp = USER_STACK + PAGE; t->frame.psr = 0;
#else
    t->frame = (struct irq_frame){.rip = entry, .cs = 0x1b, .ss = 0x23,
                                 .rflags = 0x202, .rsp = USER_STACK - 16};
#endif
    p->descriptor = descriptor; p->pid = ++next_pid; p->uid = auth_uid();
    memcpy(p->path, path, strlen(path) + 1);
    p->prev = last;
    if (last) last->next = p; else first = p;
    last = p; ready_add(p); report(p, "started", 0); return 0;
fail:
    dispose(t); memset(p, 0, PAGE); page_free(descriptor); return error;
}
static void terminate(struct application *p, int status, const char *reason) {
    if (p->state == DYING) return;
    if (p->state == RUNNABLE) ready_remove(p); else sleep_remove(p);
    p->task.wake_tick = 0; p->state = DYING;
    p->retired_next = retired; retired = p; report(p, reason, status);
}
void process_exit(int status, const char *reason) {
    struct application *p = current();
    if (p) terminate(p, status, reason);
}
void process_reap(void) {
    if (cpu_this()->index) return;
    struct application **link = &retired;
    while (*link) {
        struct application *p = *link;
        /* Architecture switches its root and flushes stale translations first. */
        if (p == current()) { link = &p->retired_next; continue; }
        *link = p->retired_next;
        if (p->prev) p->prev->next = p->next; else first = p->next;
        if (p->next) p->next->prev = p->prev; else last = p->prev;
        if (running == p) running = NULL;
        uint64_t descriptor = p->descriptor, pid = p->pid, before = pages_free();
        dispose(&p->task); memset(p, 0, PAGE); page_free(descriptor);
        log_write("PROCESS reaped; pages restored=");
        log_dec(pages_free() - before == PROCESS_PAGES);
        log_write(" pid="); log_dec(pid); log_write("\n");
    }
}
void process_session_check(void) {
    if (cpu_this()->index) return;
    uint32_t uid = auth_logged_in() ? auth_uid() : UINT32_MAX;
    /* Called by the console timer: scan only on an identity change. Admission
     * calls this too, so the cached identity covers every newly created task. */
    if (uid == observed_uid) return;
    observed_uid = uid;
    for (struct application *p = first; p; p = p->next)
        if (uid == UINT32_MAX || uid != p->uid) terminate(p, 125, "session-ended");
}
void process_tick(bool user) {
    if (cpu_this()->index) return;
    struct application *p = current();
    if (user && p && p->state == RUNNABLE) ++p->run_ticks;
    while (sleepers && sleepers->task.wake_tick <= ticks) {
        p = sleepers; sleep_remove(p); p->task.wake_tick = 0; ready_add(p);
    }
}
int64_t process_syscall(uint64_t call, uint64_t arg, uint64_t length, bool *reschedule) {
    *reschedule = false;
    struct application *p = current();
    if (!p || p->state != RUNNABLE) { *reschedule = true; return -13; }
    ++p->syscalls;
    switch (call) {
    case 0: return process_write(arg, length);
    case 1: *reschedule = true; return 0;
    case 2: return ticks;
    case 3: process_exit((int)(arg & 255), "exited"); *reschedule = true; return 0;
    case 5: {
        if (!arg || arg > 1000 || ticks > UINT64_MAX - arg) return -22;
        ready_remove(p); p->state = SLEEPING; p->task.wake_tick = ticks + arg;
        struct application *at = sleepers, *prev = NULL;
        while (at && at->task.wake_tick <= p->task.wake_tick) { prev = at; at = at->qnext; }
        p->qprev = prev; p->qnext = at;
        if (prev) prev->qnext = p; else sleepers = p;
        if (at) at->qprev = p;
        *reschedule = true; return 0;
    }
    case 6: return (int64_t)p->pid;
    case 7: return p->uid;
    case NATIVE_FILE_CALL: return file_request(&p->task, p->uid, arg, length);
    default: return -38;
    }
}
int64_t process_write(uint64_t ptr, uint64_t length) {
    struct application *p = current();
    if (!p || p->state != RUNNABLE) return -13;
    if (length > 256) return -22;
    if (!length) return 0;
    const unsigned char *s = user_buffer(&p->task, ptr, length, false);
    if (!s) return -14;
    char line[100]; size_t at = 0;
    for (uint64_t i = 0; i < length; ++i) {
        if (s[i] == '\n' || at == sizeof line - 1) {
            line[at] = 0; ui_program_output(line); at = 0;
            if (s[i] == '\n') continue;
        }
        line[at++] = s[i] >= 32 && s[i] < 127 ? (char)s[i] : '.';
    }
    if (at) { line[at] = 0; ui_program_output(line); }
    return (int64_t)length;
}
static void field(char *line, const char *label, uint64_t value) {
    size_t n = strlen(line), k = strlen(label);
    memcpy(line + n, label, k); number(line + n + k, value);
}
bool process_command(const char *cmd, void (*print)(const char *)) {
    bool status = streq(cmd, "ps"), kill = !strncmp(cmd, "kill ", 5);
    if (!status && !kill) return false;
    if (!auth_logged_in()) { print("Permission denied: sign in first"); return true; }
    if (cpu_this()->index) { print("Process control belongs to the boot CPU"); return true; }
    if (status) {
        if (!first) print("No native application running");
        for (struct application *p = first; p; p = p->next) {
            if (p->uid != auth_uid() && !auth_admin()) continue;
            char line[192] = "";
            field(line, "PID ", p->pid); field(line, " UID ", p->uid);
            field(line, " TICKS ", p->run_ticks); field(line, " SWITCHES ", p->task.switches);
            field(line, " SYSCALLS ", p->syscalls); print(line);
            print(p->state == DYING ? "STATE exiting" :
                  p->state == SLEEPING ? "STATE sleeping" : "STATE runnable");
            print(p->path);
        }
        return true;
    }
    uint64_t id = 0; const char *s = cmd + 5;
    if (!*s) { print("Usage: kill PID"); return true; }
    while (*s) {
        if (*s < '0' || *s > '9' || id > (UINT64_MAX - (*s - '0')) / 10) {
            print("Usage: kill PID"); return true;
        }
        id = id * 10 + (*s++ - '0');
    }
    struct application *p;
    for (p = first; p && p->pid != id; p = p->next) {}
    if (!p || p->state == DYING) print("No such process");
    else if (auth_uid() != p->uid && !auth_admin()) print("Permission denied");
    else terminate(p, 130, "terminated");
    return true;
}
