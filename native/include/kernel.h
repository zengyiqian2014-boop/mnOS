/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_KERNEL_H
#define MNOS_KERNEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PAGE 4096ULL
#define USER_BASE 0x100000000ULL
#define USER_DATA (USER_BASE + PAGE)
#define USER_STACK (USER_BASE + 4 * PAGE)
#define MAX_TASKS 8
#define SELFTEST_COUNT 8
#define PIT_HZ 100
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
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
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
bool streq(const char *, const char *);
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
extern volatile uint64_t ticks;
extern uint64_t kernel_pml4;
extern char cpu_brand[49];
extern unsigned pci_count;
extern bool cpu_smep, cpu_smap;
extern unsigned test_passed;
struct irq_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
struct task {
    struct irq_frame frame;
    uint64_t cr3, code, data, stack;
    uint64_t switches;
    uint64_t wake_tick;
    bool ready;
    bool pointer_checked;
    unsigned probe;
};
extern struct task tasks[MAX_TASKS];
extern unsigned current_task;
void memory_init(void);
uint64_t page_alloc(void);
void page_free(uint64_t);
uint64_t pages_free(void);
uint64_t pages_total(void);
uint64_t address_space(uint64_t code, uint64_t data, uint64_t stack);
void interrupts_init(void);
void tasks_init(void);
_Noreturn void enter_frame(struct irq_frame *);
struct irq_frame *irq_dispatch(struct irq_frame *);
void keyboard_event(uint8_t);
void mouse_event(uint8_t);
void input_init(void);
void ui_init(void);
void ui_tick(void);
void ui_key(char);
void ui_pointer(int, int, bool);
void ui_panic(const char *);
void pci_scan(void);
void pci_read_line(unsigned, char *, size_t);
void power_init(void);
void power_off(void);
void reboot(void);
void number(char *, uint64_t);
#endif
