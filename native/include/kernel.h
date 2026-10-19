/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_KERNEL_H
#define MNOS_KERNEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PAGE 4096ULL
#ifdef __aarch64__
#define MNOS_ARCH "aarch64"
#define USER_BASE 0x700000000000ULL
#else
#define MNOS_ARCH "x86_64"
#define USER_BASE 0x100000000ULL
#endif
#define USER_DATA (USER_BASE + PAGE)
#define USER_STACK (USER_BASE + 4 * PAGE)
#define PROBE_TASKS 8
/* Virtual scheduler selector for the dynamically allocated application queue. */
#define APP_TASK PROBE_TASKS
#define MAX_TASKS (PROBE_TASKS + 1)
#define SELFTEST_COUNT 8
#define PIT_HZ 100
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#ifdef __aarch64__
#include "../arch/arm64/primitives.h"
#else
static inline uint64_t spin_lock(unsigned *lock) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
    return flags;
}
static inline void spin_unlock(unsigned *lock, uint64_t flags) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p));
}
static inline uint32_t inl(uint16_t p) {
    uint32_t v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr2,%0" : "=r"(v));
    return v;
}
static inline void cr3_set(uint64_t v) {
    __asm__ volatile("mov %0,%%cr3" ::"r"(v) : "memory");
}
static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c,
                         uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
#endif
bool arch_random(uint8_t *, size_t);
#ifndef __aarch64__
void rng_pci_probe(unsigned, unsigned, unsigned, uint16_t, uint16_t);
bool virtio_random(uint8_t *, size_t);
#endif
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
bool streq(const char *, const char *);
int strncmp(const char *, const char *, size_t);
void log_write(const char *);
void log_hex(uint64_t);
void log_dec(uint64_t);
_Noreturn void panic(const char *);
struct mb_tag {
    uint32_t type, size;
};
struct mb_mmap_entry {
    uint64_t address, length;
    uint32_t type, reserved;
} __attribute__((packed));
struct mb_mmap {
    struct mb_tag tag;
    uint32_t entry_size, version;
    struct mb_mmap_entry entries[];
};
struct framebuffer {
    uint32_t *pixels;
    uint32_t width, height, pitch;
    uint8_t red, green, blue;
};
struct boot_info {
    struct mb_mmap *map;
    struct framebuffer fb;
    uintptr_t mb_start, mb_end;
    bool selftest;
    const void *rsdp;
};
extern struct boot_info boot;
extern uint64_t kernel_pml4;
extern char cpu_brand[49];
extern unsigned pci_count;
extern bool cpu_smep, cpu_smap;
#ifdef __aarch64__
struct irq_frame { uint64_t x[31], sp, pc, psr; };
#else
struct irq_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
#endif
struct task {
    struct irq_frame frame;
#ifdef __aarch64__
    uint64_t thread_pointer; /* TPIDR_EL0 belongs to this user context. */
#endif
    uint64_t cr3, code, data, stack;
    uint64_t switches;
    uint64_t wake_tick;
    bool ready;
    bool pointer_checked;
    unsigned probe;
};
struct tss64 {
    uint32_t r0;
    uint64_t rsp[3], r1, ist[7], r2;
    uint16_t r3, iomap;
} __attribute__((packed));
struct cpu {
    struct cpu *self, *next;
    uint32_t apic_id;
#ifdef __aarch64__
    uint64_t hardware_id;
#endif
    uint64_t index;
    volatile unsigned online, completed;
    uint64_t clock_ticks, idle_wakes, probe_counts[2];
    unsigned current, checks;
    bool preemption_checked;
    struct task work[MAX_TASKS]; /* Per-CPU test/work slots; not a CPU-count limit. */
    uint64_t gdt[7];
    struct tss64 tss;
    uintptr_t irq_top, fault_top;
};
#ifdef MNOS_HOST_PROCESS_TEST
struct cpu *cpu_this(void);
#else
static inline struct cpu *cpu_this(void) {
    struct cpu *cpu;
#ifdef __aarch64__
    __asm__ volatile("mrs %0,tpidr_el1" : "=r"(cpu));
#else
    __asm__ volatile("movq %%gs:0,%0" : "=r"(cpu));
#endif
    return cpu;
}
#endif
#define ticks (cpu_this()->clock_ticks)
#define tasks (cpu_this()->work)
#define current_task (cpu_this()->current)
#define test_passed (cpu_this()->checks)
void smp_bootstrap(void);
void smp_prepare(void);
void smp_start(void);
void smp_tick_complete(void);
uint64_t cpu_count(void);
uint64_t cpu_online_count(void);
void lapic_eoi(void);
void interrupts_cpu_init(void);
void tasks_init_for(struct cpu *);
void memory_init(void);
void memory_cpu_init(void);
void memory_protect_low(uint64_t, bool);
void *phys_pointer(uint64_t);
void *kernel_alloc(size_t);
uint64_t memory_limit(void);
uint64_t page_alloc_between(uint64_t, uint64_t);
const void *firmware_pointer(uint64_t, size_t);
uint64_t page_alloc(void);
uint64_t page_try_alloc(void);
void page_free(uint64_t);
uint64_t pages_free(void);
uint64_t pages_total(void);
uint64_t address_space(uint64_t code, uint64_t data, uint64_t stack);
uint64_t address_space_try(uint64_t code, uint64_t data, uint64_t stack);
void address_space_destroy(uint64_t root);
void interrupts_init(void);
void tasks_init(void);
_Noreturn void enter_frame(struct irq_frame *);
struct irq_frame *irq_dispatch(struct irq_frame *);
void keyboard_event(uint8_t);
void mouse_event(uint8_t);
void input_init(void);
void ui_init(void);
void files_init(void);
void files_session(void);
struct mnfs;
struct mnfs *files_volume(const char **);
bool files_command(const char *, void (*)(const char *));
void ui_tick(void);
void ui_key(char);
void ui_pointer(int, int, bool);
void ui_program_output(const char *);
void ui_panic(const char *);
void pci_scan(void);
bool graphics_available(void);
bool graphics_mode(unsigned, unsigned);
const char *graphics_driver(void);
void ui_load_settings(void);
int files_settings(unsigned *, unsigned *, unsigned *, bool);
void pci_read_line(unsigned, char *, size_t);
void power_init(void);
void power_off(void);
void reboot(void);
void number(char *, uint64_t);
#endif
