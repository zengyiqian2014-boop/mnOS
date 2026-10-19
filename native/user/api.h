/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_USER_API_H
#define MNOS_USER_API_H
#include <stdint.h>
#include <stddef.h>
static inline int64_t native_call(uint64_t op, uint64_t arg, uint64_t len) {
#ifdef __aarch64__
    register uint64_t x8 __asm__("x8") = op;
    register uint64_t x0 __asm__("x0") = arg;
    register uint64_t x1 __asm__("x1") = len;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory", "cc");
    return (int64_t)x0;
#else
    uint64_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(op), "D"(arg), "S"(len) : "memory", "cc");
    return (int64_t)result;
#endif
}
static inline int64_t native_write(const char *p, size_t n) {
    return native_call(0, (uintptr_t)p, n);
}
static inline int64_t native_getpid(void) { return native_call(6, 0, 0); }
static inline int64_t native_getuid(void) { return native_call(7, 0, 0); }
static inline _Noreturn void native_exit(unsigned status) {
    native_call(3, status, 0);
    for (;;) {}
}
#endif
