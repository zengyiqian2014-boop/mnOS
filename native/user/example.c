/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original freestanding applications; no libc and no Linux system calls. */
#include "api.h"
#ifdef __aarch64__
#define BASE 0x700000000000ULL
#else
#define BASE 0x100000000ULL
#endif
#ifdef PROGRAM_filetest
static unsigned char bytes[64];
static char path[96];
static const unsigned char payload[] = {0, 255, 'm', 'n', 'O', 'S', 1};
static void check(int ok) { if (!ok) native_exit(110); }
_Noreturn void _start(void) {
    int64_t home = native_file(NATIVE_FILE_HOME, 0, path, sizeof path, 1);
    check(home > 0 && home < 64);
    const char suffix[] = "/native-file.bin";
    for (unsigned i = 0; i < sizeof suffix; ++i) path[home + i] = suffix[i];
    uint64_t offset = (1ULL << 45) + 4094;
    int64_t created = native_file(NATIVE_FILE_CREATE, path, 0, 0, 0);
    check(created == 0 || created == -17);
    if (!created) {
        check(native_file(NATIVE_FILE_WRITE, path, (void *)payload, sizeof payload, offset) == sizeof payload);
    } else {
        static const char persisted[] = "FILE: previous boot data present\n";
        native_write(persisted, sizeof persisted - 1);
    }
    check(native_file(NATIVE_FILE_READ, path, bytes, sizeof payload, offset) == sizeof payload);
    for (unsigned i = 0; i < sizeof payload; ++i) check(bytes[i] == payload[i]);
    struct native_file_stat st;
    check(!native_file(NATIVE_FILE_STAT, path, &st, sizeof st, 0));
    check(st.size == offset + sizeof payload && st.inode && !st.directory);
    check(native_file(NATIVE_FILE_READ, path, bytes, 1, st.size) == 0);
    check(native_file(NATIVE_FILE_READ, path, (void *)(uintptr_t)BASE, 1, offset) == -14);
    check(native_file(NATIVE_FILE_WRITE, path, (void *)(uintptr_t)(BASE + 8191), 2, 0) == -14);
    check(native_call(NATIVE_FILE_CALL, BASE + 8192, sizeof(struct native_file_request)) == -14);
    check(native_file(NATIVE_FILE_READ, "/disk/system/accounts.bin", bytes, 1, 0) == -13);
    check(native_file(NATIVE_FILE_READ, "/disk/root/hidden", bytes, 1, 0) == -13);
    check(native_file(NATIVE_FILE_CREATE, "/disk/home/demo/../root/escape", 0, 0, 0) == -13);
    home = native_file(NATIVE_FILE_HOME, 0, path, sizeof path, 0);
    check(home > 0 && home < 64);
    for (unsigned i = 0; i < sizeof suffix; ++i) path[home + i] = suffix[i];
    check(!native_file(NATIVE_FILE_CREATE, path, 0, 0, 0));
    check(native_file(NATIVE_FILE_WRITE, path, (void *)payload, sizeof payload, 0) == sizeof payload);
    check(!native_file(NATIVE_FILE_TRUNCATE, path, 0, 0, 3));
    check(native_file(NATIVE_FILE_READ, path, bytes, sizeof bytes, 0) == 3);
    check(!native_file(NATIVE_FILE_REMOVE, path, 0, 0, 0));
    check(native_file(NATIVE_FILE_READ, path, bytes, 1, 0) == -2);
    static const char ok[] = "FILE: native user read/write, sparse offsets and permissions passed\n";
    native_write(ok, sizeof ok - 1);
    native_exit(0);
}
#endif
#ifdef PROGRAM_hello
static volatile unsigned initialized = 42;
static volatile unsigned bss[16];
_Noreturn void _start(void) {
    if (initialized != 42) native_exit(90);
    for (unsigned i=0;i<16;i++) if (bss[i]) native_exit(91);
    bss[3] = 17;
    static const char msg[] = "HELLO: native user ELF; initialized data and BSS OK\n";
    native_write(msg, sizeof msg - 1);
    if (native_call(5, 2, 0)) native_exit(92);
    native_exit(bss[3] == 17 ? 7 : 93);
}
#endif
#ifdef PROGRAM_fault
_Noreturn void _start(void) {
    static const char msg[] = "FAULT: trying to write read-only user code\n";
    native_write(msg, sizeof msg - 1);
    *(volatile unsigned *)(uintptr_t)BASE = 0;
    native_exit(94);
}
#endif
#ifdef PROGRAM_spin
_Noreturn void _start(void) {
    static const char msg[] = "SPIN: running without yielding; use kill PID\n";
    native_write(msg, sizeof msg - 1);
    for (;;) __asm__ volatile("" ::: "memory");
}
#endif
#ifdef PROGRAM_badptr
_Noreturn void _start(void) {
    if (native_call(0, 0x100000, 16) != -14 ||
        native_call(0, BASE+8192, 1) != -14 ||
        native_call(0, BASE+4095, 2) != -14 ||
        native_call(0, UINT64_MAX-1, 8) != -14 ||
        native_call(0, BASE, 257) != -22 ||
        native_call(88, 0, 0) != -38 ||
        native_call(4, 0, 0) != -38 ||
        native_call(5, 0, 0) != -22) native_exit(95);
    static const char msg[] = "BADPTR: boundaries and unknown syscalls rejected\n";
    native_write(msg, sizeof msg - 1);
    native_exit(0);
}
#endif
#ifdef PROGRAM_isolate
static volatile uint64_t identity[128];
_Noreturn void _start(void) {
    uint64_t pid = native_getpid(), uid = native_getuid();
    if (!pid || uid != 1000) native_exit(96);
#ifdef __aarch64__
    uint64_t thread_pointer;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(thread_pointer));
    if (thread_pointer) native_exit(102);
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(pid) : "memory");
#endif
    for (unsigned i = 0; i < 128; ++i) {
        if (identity[i]) native_exit(97);
        identity[i] = pid * 131 + i;
    }
    static const char started[] = "ISOLATE: private BSS initialized\n";
    native_write(started, sizeof started - 1);
    for (unsigned round = 0;; ++round) {
        if (native_call(5, 20 + pid % 5, 0)) native_exit(98);
        for (unsigned i = 0; i < 128; ++i)
            if (identity[i] != pid * 131 + i) native_exit(99);
        if (native_getpid() != (int64_t)pid || native_getuid() != (int64_t)uid) native_exit(100);
#ifdef __aarch64__
        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(thread_pointer));
        if (thread_pointer != pid) native_exit(103);
#endif
        if (round == 4) {
            static const char ok[] = "ISOLATE: identity and private data survived 5 sleeps\n";
            native_write(ok, sizeof ok - 1);
        }
    }
}
#endif
#if defined(PROGRAM_fpstate) || defined(PROGRAM_simdstate) || defined(PROGRAM_tlsstate)
_Noreturn void _start(void) {
    /* Extended state is deliberately unavailable until context save/restore is
     * implemented. These instructions must fault only this process. */
#ifdef PROGRAM_tlsstate
#ifdef __aarch64__
    __asm__ volatile("msr tpidrro_el0, xzr");
#else
    __asm__ volatile("xor %eax, %eax; wrfsbase %rax");
#endif
#elif defined(PROGRAM_fpstate)
#ifdef __aarch64__
    /* Explicit encodings keep the rest of this probe -mgeneral-regs-only. */
    __asm__ volatile(".inst 0x1e6e1000"); /* fmov d0, #1.0 */
#else
    __asm__ volatile("fldz");
#endif
#else
#ifdef __aarch64__
    __asm__ volatile(".inst 0x4f00e400"); /* movi v0.16b, #0 */
#else
    __asm__ volatile("pxor %xmm0, %xmm0");
#endif
#endif
    static const char failed[] = "EXTENDED STATE TRAP FAILED\n";
    native_write(failed, sizeof failed - 1);
    native_exit(101);
}
#endif
