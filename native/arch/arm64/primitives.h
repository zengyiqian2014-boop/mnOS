/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_ARM_PRIMITIVES_H
#define MNOS_ARM_PRIMITIVES_H
#define sys_read(reg)                                                                              \
    ({                                                                                             \
        uint64_t v;                                                                                \
        __asm__ volatile("mrs %0," #reg : "=r"(v));                                                \
        v;                                                                                         \
    })
#define sys_write(reg, v) __asm__ volatile("msr " #reg ",%0" ::"r"((uint64_t)(v)) : "memory")
static inline uint64_t spin_lock(unsigned *lock) {
    uint64_t flags = sys_read(daif);
    __asm__ volatile("msr daifset,#2" ::: "memory");
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("yield");
    return flags;
}
static inline void spin_unlock(unsigned *lock, uint64_t flags) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    sys_write(daif, flags);
}
static inline void barrier(void) {
    __asm__ volatile("dsb sy; isb" ::: "memory");
}
static inline uint32_t mmio_read(uintptr_t p) {
    return *(volatile uint32_t *)p;
}
static inline void mmio_write(uintptr_t p, uint32_t n) {
    *(volatile uint32_t *)p = n;
}
#endif
