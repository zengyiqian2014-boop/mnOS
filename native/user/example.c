/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original freestanding applications; no libc and no Linux system calls. */
#include "api.h"
#ifdef __aarch64__
#define BASE 0x700000000000ULL
#else
#define BASE 0x100000000ULL
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
