/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the production process manager/ELF loader. Simulated CPU, physical
 * allocator and page tables let ASan/UBSan cover failures at every allocation. */
#include "kernel.h"
#include "process.h"
#include "auth.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define ARENA_PAGES 8192
static unsigned char arena[ARENA_PAGES][4096] __attribute__((aligned(4096)));
static unsigned char used[ARENA_PAGES], table[ARENA_PAGES];
static unsigned available = ARENA_PAGES, allocated, calls, fail_at;
static uint64_t active_root;
static struct cpu cpu;
static uint32_t uid = 1000;
static bool logged = true;
static char output[1024 * 1024];
static size_t output_used;
static unsigned char image[1024];
struct cpu *cpu_this(void) { return &cpu; }
bool auth_logged_in(void) { return logged; }
bool auth_admin(void) { return logged && !uid; }
uint32_t auth_uid(void) { return uid; }
bool streq(const char *a, const char *b) { return !strcmp(a, b); }
void number(char *s, uint64_t n) { sprintf(s, "%" PRIu64, n); }
void log_write(const char *s) {
    size_t n = strlen(s); assert(output_used + n + 1 < sizeof output);
    memcpy(output + output_used, s, n + 1); output_used += n;
}
void log_dec(uint64_t n) { char s[24]; number(s, n); log_write(s); }
void ui_program_output(const char *s) { log_write(s); log_write("\n"); }
static void clear_output(void) { output_used = 0; output[0] = 0; }
void *phys_pointer(uint64_t p) {
    assert(p >= PAGE && p < (ARENA_PAGES + 1) * PAGE);
    return &arena[(p - PAGE) / PAGE][p % PAGE];
}
uint64_t pages_free(void) { return available - allocated; }
uint64_t page_try_alloc(void) {
    if (++calls == fail_at) return 0;
    for (unsigned i = 0; i < available; ++i) if (!used[i]) {
        used[i] = 1; ++allocated; memset(arena[i], 0, PAGE);
        return (i + 1) * PAGE;
    }
    return 0;
}
void page_free(uint64_t p) {
    assert(p % PAGE == 0 && p >= PAGE && p <= ARENA_PAGES * PAGE);
    unsigned i = p / PAGE - 1; assert(used[i]);
    if (!table[i]) /* Payload and descriptor contents must be wiped before free. */
        for (unsigned n = 0; n < PAGE; ++n) assert(arena[i][n] == 0);
    used[i] = table[i] = 0; --allocated;
    memset(arena[i], 0xa5, PAGE); /* Catch reliance on recycled contents. */
}
uint64_t address_space_try(uint64_t code, uint64_t data, uint64_t stack) {
    assert(code && data && stack);
    uint64_t p[4] = {0};
    for (unsigned n = 0; n < 4; ++n) {
        p[n] = page_try_alloc();
        if (!p[n]) {
            while (n) page_free(p[--n]);
            return 0;
        }
        table[p[n] / PAGE - 1] = 1;
    }
    memcpy(phys_pointer(p[0]), p, sizeof p); return p[0];
}
void address_space_destroy(uint64_t root) {
    assert(root && root != active_root); /* Never free the active translation root. */
    uint64_t p[4]; memcpy(p, phys_pointer(root), sizeof p);
    for (unsigned n = 0; n < 4; ++n) page_free(p[n]);
}
static void put(size_t off, uint64_t value, size_t n) {
    for (size_t i = 0; i < n; ++i) image[off + i] = (unsigned char)(value >> (i * 8));
}
static void fixture(void) {
    memcpy(image, "\177ELF\2\1\1", 7);
    put(16, 2, 2); put(18, 62, 2); put(20, 1, 4); put(24, USER_BASE, 8);
    put(32, 64, 8); put(52, 64, 2); put(54, 56, 2); put(56, 2, 2);
    put(64, 1, 4); put(68, 5, 4); put(72, 256, 8); put(80, USER_BASE, 8);
    put(96, 16, 8); put(104, 32, 8); put(112, 1, 8);
    put(120, 1, 4); put(124, 6, 4); put(128, 512, 8); put(136, USER_DATA, 8);
    put(152, 16, 8); put(160, PAGE, 8); put(168, 1, 8);
    memset(image + 256, 0x90, 16); memset(image + 512, 0x42, 16);
}
static int start(void) { return process_start(image, sizeof image, "/ram/bin/test"); }
static void boot_context(void) { cpu.current = 0; active_root = 1; }
static struct task *choose(void) {
    struct task *t = process_choose();
    if (t) { cpu.current = APP_TASK; active_root = t->cr3; ++t->switches; }
    else boot_context();
    process_reap(); return t;
}
static uint64_t pid(void) {
    bool reschedule;
    int64_t id = process_syscall(6, 0, 0, &reschedule);
    assert(id > 0 && !reschedule); return (uint64_t)id;
}
static int64_t call(uint64_t op, uint64_t a, uint64_t b, bool expected_schedule) {
    bool reschedule = !expected_schedule;
    int64_t result = process_syscall(op, a, b, &reschedule);
    assert(reschedule == expected_schedule); return result;
}
static void kill_pid(uint64_t id) {
    char cmd[40]; snprintf(cmd, sizeof cmd, "kill %" PRIu64, id);
    assert(process_command(cmd, ui_program_output));
}
static void cleanup(void) {
    logged = false; process_session_check(); boot_context(); process_reap();
    assert(!process_active() && !process_sleeping() && !choose());
    assert(!allocated); logged = true; uid = 1000; clear_output();
}
static void allocation_failures(void) {
    assert(!start()); struct task *survivor = choose(); uint64_t id = pid();
    for (unsigned n = 1; n <= 8; ++n) {
        unsigned before = allocated; fail_at = calls + n;
        assert(start() == -12); fail_at = 0;
        assert(allocated == before && choose() == survivor && pid() == id);
    }
    image[18] = 0; assert(start() == -8); image[18] = 62;
    assert(allocated == 8);
    logged = false; assert(start() == -13); logged = true;
    cpu.completed = 0; assert(start() == -16); cpu.completed = 1;
    cpu.index = 1; assert(start() == -13 && !process_choose()); cpu.index = 0;
    assert(process_start(NULL, sizeof image, "/test") == -8);
    assert(process_start(image, EXEC_FILE_LIMIT + 1, "/test") == -8);
    cleanup(); puts("PASS: eight allocation failures, ELF failure rollback, privilege/boot gates");
}
static void parallel(void) {
    struct task *list[512]; uint64_t ids[512];
    for (unsigned n = 0; n < 512; ++n) assert(!start());
    for (unsigned n = 0; n < 512; ++n) {
        list[n] = choose(); ids[n] = pid();
        if (n) assert(ids[n] > ids[n - 1]);
        assert(call(7, 0, 0, false) == 1000);
        uint64_t *data = phys_pointer(list[n]->data);
        for (unsigned k = 2; k < PAGE / 8; ++k) assert(!data[k]);
        data[20] = ids[n];
    }
    for (unsigned round = 0; round < 16; ++round)
        for (unsigned n = 0; n < 512; ++n) {
            assert(choose() == list[n] && pid() == ids[n]);
            assert(((uint64_t *)phys_pointer(list[n]->data))[20] == ids[n]);
        }
    assert(allocated == 512 * 8);
    /* A faulted current process is retained until the context/root changes. */
    process_exit(139, "fault"); process_reap(); assert(allocated == 512 * 8);
    assert(choose() == list[0] && allocated == 511 * 8);
    for (unsigned n = 0; n < 511; n += 2) kill_pid(ids[n]);
    boot_context(); process_reap();
    for (unsigned n = 1; n < 511; n += 2) {
        assert(choose() == list[n]); kill_pid(ids[n]);
    }
    boot_context(); process_reap(); assert(!allocated);
    uint64_t old = ids[511]; assert(!start()); choose(); assert(pid() > old);
    kill_pid(old); assert(!process_dying());
    cleanup(); puts("PASS: 512 concurrent processes, fair queue, private pages, deferred wipe/reap, stale PID");
}
static void sleeping(void) {
    uint64_t ids[5]; const unsigned delays[] = {30, 10, 20, 10, 40};
    cpu.clock_ticks = 100;
    for (unsigned n = 0; n < 5; ++n) assert(!start());
    for (unsigned n = 0; n < 5; ++n) {
        assert(choose()); ids[n] = pid();
        assert(!call(5, delays[n], 0, true));
    }
    assert(!choose() && process_sleeping());
    kill_pid(ids[2]); process_reap(); assert(allocated == 32);
    cpu.clock_ticks = 109; process_tick(false); assert(!choose());
    cpu.clock_ticks = 110; process_tick(false);
    assert(choose() && pid() == ids[1]);
    assert(choose() && pid() == ids[3]);
    assert(choose() && pid() == ids[1]);
    kill_pid(ids[1]); kill_pid(ids[3]); boot_context(); process_reap();
    cpu.clock_ticks = 129; process_tick(false); assert(!choose());
    cpu.clock_ticks = 130; process_tick(false); assert(choose() && pid() == ids[0]);
    assert(call(5, 0, 0, false) == -22 && call(5, 1001, 0, false) == -22);
    cpu.clock_ticks = UINT64_MAX - 1; assert(call(5, 2, 0, false) == -22);
    cpu.clock_ticks = 140; process_tick(false); kill_pid(ids[0]);
    assert(choose() && pid() == ids[4]);
    assert(!process_sleeping());
    cleanup(); puts("PASS: sorted/tied sleep deadlines, no early wake, sleeping kill, overflow rejection");
}
static void permissions(void) {
    assert(!start()); struct task *t = choose(); uint64_t id = pid();
    assert(call(99, 0, 0, false) == -38);
    assert(call(0, UINT64_MAX - 1, 8, false) == -14);
    assert(call(0, USER_BASE + PAGE - 1, 2, false) == -14);
    assert(call(0, USER_BASE, 257, false) == -22);
    assert(call(0, USER_BASE + 2 * PAGE, 1, false) == -14);
    memcpy(phys_pointer(t->data), "owned", 5);
    clear_output(); assert(call(0, USER_DATA, 5, false) == 5);
    assert(strstr(output, "owned"));
    uid = 1001; clear_output(); kill_pid(id);
    assert(strstr(output, "Permission denied") && !process_dying());
    clear_output(); process_command("ps", ui_program_output); assert(!strstr(output, "PID "));
    uid = 0; process_command("ps", ui_program_output); assert(strstr(output, "UID 1000"));
    kill_pid(id); assert(process_dying()); cleanup();
    for (unsigned n = 0; n < 4; ++n) assert(!start());
    choose(); assert(!call(5, 10, 0, true)); choose();
    uid = 1001; process_session_check(); assert(process_dying());
    boot_context(); process_reap(); assert(!allocated); cleanup();
    puts("PASS: pointer bounds, syscall allowlist, PID owner/root checks, session revocation");
}
static void pressure(void) {
    available = 1024; unsigned count = 0;
    while (!start()) ++count;
    assert(count == 120 && pages_free() == 64);
    assert(choose()); uint64_t id = pid(); kill_pid(id);
    boot_context(); process_reap(); assert(!start() && pages_free() == 64);
    cleanup(); available = ARENA_PAGES;
    for (unsigned n = 0; n < 1000; ++n) {
        assert(!start()); choose(); process_exit(0, "exited");
        boot_context(); process_reap(); assert(!allocated);
    }
    cleanup(); puts("PASS: kernel reserve under pressure, recovery, 1000 churn cycles without page leaks");
}
static double time_picks(unsigned count) {
    for (unsigned n = 0; n < count; ++n) assert(!start());
    struct timespec a, b; timespec_get(&a, TIME_UTC);
    for (unsigned n = 0; n < 1000000; ++n) assert(process_choose());
    timespec_get(&b, TIME_UTC);
    double ns = (b.tv_sec - a.tv_sec) * 1e9 + b.tv_nsec - a.tv_nsec;
    cleanup(); return ns / 1000000;
}
int main(void) {
    cpu.completed = 1; boot_context(); fixture();
    allocation_failures(); parallel(); sleeping(); permissions(); pressure();
    double one = time_picks(1), many = time_picks(512);
    printf("BENCH (host sanitizer, no context switch): ready-queue selection 1 process %.1f ns, 512 processes %.1f ns\n", one, many);
    puts("PASS: production dynamic process manager under ASan/UBSan");
}
