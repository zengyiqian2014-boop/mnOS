/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
struct boot_info boot;
char cpu_brand[49];
bool cpu_smep, cpu_smap;

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        ++x;
        ++y;
    }
    return 0;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n--) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
bool streq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
void number(char *s, uint64_t n) {
    char tmp[21];
    unsigned i = 0, j = 0;
    do {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i)
        s[j++] = tmp[--i];
    s[j] = 0;
}
static void serial_init(void) {
    outb(0x3f9, 0);
    outb(0x3fb, 0x80);
    outb(0x3f8, 1);
    outb(0x3f9, 0);
    outb(0x3fb, 3);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}
void log_write(const char *s) {
    static unsigned lock;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
    while (*s) {
        unsigned wait = 100000;
        while (!(inb(0x3fd) & 0x20) && --wait) {
        }
        outb(0x3f8, (uint8_t)*s++);
    }
    __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}
void log_dec(uint64_t n) {
    char b[21];
    number(b, n);
    log_write(b);
}
void log_hex(uint64_t n) {
    char b[19] = "0x";
    for (unsigned i = 0; i < 16; ++i)
        b[i + 2] = "0123456789abcdef"[(n >> (60 - i * 4)) & 15];
    b[18] = 0;
    log_write(b);
}
_Noreturn void panic(const char *s) {
    __asm__ volatile("cli");
    log_write("\nPANIC: ");
    log_write(s);
    log_write("\n");
    ui_panic(s);
    if (boot.selftest)
        outl(0xf4, 0x11);
    for (;;)
        __asm__ volatile("hlt");
}
static bool token(const char *s, const char *wanted) {
    size_t n = strlen(wanted);
    while (*s) {
        while (*s == ' ')
            ++s;
        size_t k = 0;
        while (s[k] && s[k] != ' ')
            ++k;
        if (k == n && !memcmp(s, wanted, n))
            return true;
        s += k;
    }
    return false;
}
static void parse_boot(uintptr_t address) {
    if (address & 7)
        panic("unaligned multiboot structure");
    uint32_t total = *(uint32_t *)address;
    if (total < 16 || total > 1024 * 1024 || address + total >= 0x100000000ULL)
        panic("invalid multiboot extent");
    boot.mb_start = address;
    boot.mb_end = address + total;
    for (uintptr_t p = address + 8; p + 8 <= boot.mb_end;) {
        struct mb_tag *tag = (void *)p;
        if (tag->size < 8 || tag->size > boot.mb_end - p)
            panic("invalid boot tag");
        if (tag->type == 0)
            break;
        if (tag->type == 1 && tag->size > 8) {
            const char *cmd = (void *)(p + 8);
            bool end = false;
            for (size_t i = 0; i < tag->size - 8; ++i)
                if (!cmd[i]) {
                    end = true;
                    break;
                }
            if (end)
                boot.selftest = token(cmd, "mnos.selftest=1");
        }
        if (tag->type == 6) {
            struct mb_mmap *m = (void *)p;
            if (tag->size < 16 || m->entry_size < sizeof(struct mb_mmap_entry) ||
                (tag->size - 16) % m->entry_size)
                panic("invalid memory map");
            boot.map = m;
        }
        if (tag->type == 8 && tag->size >= 38) {
            struct fb_tag {
                struct mb_tag t;
                uint64_t addr;
                uint32_t pitch, w, h;
                uint8_t bpp, type;
                uint16_t reserved;
                uint8_t rp, rs, gp, gs, bp, bs;
            } __attribute__((packed));
            const struct fb_tag *f = (void *)p;
            if (f->bpp == 32 && f->type == 1 && f->w >= 640 && f->h >= 480 && f->w <= 2560 &&
                f->h <= 1440 && f->pitch >= f->w * 4 && !(f->pitch & 3) &&
                f->addr < 0x100000000ULL && (uint64_t)f->pitch * f->h <= 0x100000000ULL - f->addr &&
                f->rs == 8 && f->gs == 8 && f->bs == 8 && f->rp <= 24 && f->gp <= 24 &&
                f->bp <= 24 && f->rp != f->gp && f->rp != f->bp && f->gp != f->bp) {
                boot.fb = (struct framebuffer){
                    (void *)(uintptr_t)f->addr, f->w, f->h, f->pitch, f->rp, f->gp, f->bp};
            }
        }
        if (tag->type == 14 && tag->size >= 28 && !boot.rsdp)
            boot.rsdp = (void *)(p + 8);
        if (tag->type == 15 && tag->size >= 44)
            boot.rsdp = (void *)(p + 8);
        p += (tag->size + 7ULL) & ~7ULL;
    }
    if (!boot.map)
        panic("bootloader did not supply a memory map");
}
void kernel_main(uintptr_t address) {
    serial_init();
    smp_bootstrap();
    log_write("mnOS native 0.1 / x86_64\n");
    parse_boot(address);
    uint32_t a, b, c, d;
    cpuid(0x80000000, 0, &a, &b, &c, &d);
    if (a >= 0x80000004)
        for (unsigned i = 0; i < 3; ++i) {
            cpuid(0x80000002 + i, 0, &a, &b, &c, &d);
            memcpy(cpu_brand + i * 16, &a, 4);
            memcpy(cpu_brand + i * 16 + 4, &b, 4);
            memcpy(cpu_brand + i * 16 + 8, &c, 4);
            memcpy(cpu_brand + i * 16 + 12, &d, 4);
        }
    cpu_brand[48] = 0;
    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        cpu_smep = (b & (1u << 7)) != 0;
        cpu_smap = (b & (1u << 20)) != 0;
    }
    memory_init();
    log_write("PASS: physical page allocator / kernel W^X / null and stack guards\n");
    ++test_passed;
    interrupts_init();
    pci_scan();
    power_init();
    input_init();
    ui_init();
    files_init();
    smp_prepare();
    tasks_init();
    smp_start();
    log_write("BOOT: entering ring 3; per-CPU preemption (PIT/LAPIC) at 100 Hz\n");
    cr3_set(tasks[0].cr3);
    enter_frame(&tasks[0].frame);
}
