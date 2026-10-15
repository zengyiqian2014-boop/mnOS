/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "auth.h"
#include "platform.h"
struct boot_info boot;
struct arm_platform platform;
char cpu_brand[49] = "ARM64 / original mnOS kernel";
bool cpu_smep, cpu_smap;
unsigned pci_count;
static struct cpu primary;
static uint64_t discovered = 1;
void log_write(const char *s) {
    if (!platform.uart)
        return;
    static unsigned lock;
    uint64_t flags = spin_lock(&lock);
    while (*s) {
        unsigned wait = 1000000;
        while ((mmio_read(platform.uart + 0x18) & 32) && --wait) {
        }
        mmio_write(platform.uart, (uint8_t)*s++);
    }
    spin_unlock(&lock, flags);
}
void log_dec(uint64_t n) {
    char b[24];
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
int64_t arm_psci(uint64_t fn, uint64_t a, uint64_t b, uint64_t c) {
    register uint64_t x0 __asm__("x0") = fn, x1 __asm__("x1") = a, x2 __asm__("x2") = b,
                         x3 __asm__("x3") = c;
    if (platform.psci_hvc)
        __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)::"memory");
    else
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)::"memory");
    return (int64_t)x0;
}
_Noreturn void panic(const char *s) {
    __asm__ volatile("msr daifset,#15");
    log_write("PANIC: ");
    log_write(s);
    log_write("\n");
    ui_panic(s);
    if (boot.selftest)
        arm_psci(0x84000008, 0, 0, 0);
    for (;;)
        __asm__ volatile("wfi");
}
void power_off(void) {
    log_write("POWER: PSCI SYSTEM_OFF\n");
    arm_psci(0x84000008, 0, 0, 0);
    panic("PSCI SYSTEM_OFF returned");
}
void reboot(void) {
    arm_psci(0x84000009, 0, 0, 0);
    panic("PSCI SYSTEM_RESET returned");
}
uint64_t cpu_count(void) {
    return discovered;
}
uint64_t cpu_online_count(void) {
    uint64_t n = 0;
    for (struct cpu *c = &primary; c; c = c->next)
        n += __atomic_load_n(&c->online, __ATOMIC_ACQUIRE) != 0;
    return n;
}
static void discover(uint64_t id) {
    uint64_t own = sys_read(mpidr_el1) & 0xff00ffffffULL;
    if (id == own)
        return;
    for (struct cpu *c = &primary; c; c = c->next)
        if (c->hardware_id == id)
            return;
    struct cpu *c = (void *)page_alloc();
    if (sizeof *c > PAGE)
        panic("CPU context size");
    memset(c, 0, sizeof *c);
    c->hardware_id = id;
    c->index = discovered++;
    c->irq_top = (uintptr_t)kernel_alloc(65536) + 65536;
    c->self = (void *)(page_alloc() + PAGE);
    c->next = primary.next;
    primary.next = c;
    tasks_init_for(c);
}
void smp_tick_complete(void) {
    if (cpu_this()->checks != SELFTEST_COUNT || cpu_this()->completed)
        return;
    char line[96] = "ARM CPU ";
    number(line + 8, cpu_this()->index);
    size_t at = strlen(line);
    memcpy(line + at, " SELFTEST PASS 8/8\n", 20);
    log_write(line);
    __atomic_store_n(&cpu_this()->completed, 1, __ATOMIC_RELEASE);
}
static void start_cpus(void) {
    if (platform.fdt)
        arm_fdt_cpus(discover);
    else
        arm_acpi_cpus(discover);
    log_write("ARM SMP: discovered ");
    log_dec(discovered);
    log_write(" CPUs\n");
    for (struct cpu *c = primary.next; c; c = c->next) {
        barrier();
        int64_t result =
            arm_psci(0xc4000003, c->hardware_id, (uintptr_t)arm_secondary, (uintptr_t)c);
        if (result) {
            log_write("PSCI CPU_ON status ");
            log_hex(result);
            panic("secondary startup rejected");
        }
    }
}
_Noreturn void arm_ap_main(struct cpu *c) {
    c->self = c;
    sys_write(tpidr_el1, (uintptr_t)c);
    arm_mmu_on(arm_root);
    sys_write(vbar_el1, (uintptr_t)arm_vectors);
    arm_irq_cpu();
    __atomic_store_n(&c->online, 1, __ATOMIC_RELEASE);
    c->checks = 1;
    arm_timer();
    extern void arm_enter(struct irq_frame *);
    struct irq_frame *f = (void *)(c->irq_top - 288);
    memcpy(f, &c->work[0].frame, sizeof *f);
    sys_write(ttbr0_el1, c->work[0].cr3);
    barrier();
    arm_enter(f);
    __builtin_unreachable();
}
void arm_input_poll(void) {
    while (!(mmio_read(platform.uart + 0x18) & 16)) {
        char c = mmio_read(platform.uart) & 255;
        ui_key(c);
    }
    arm_virtio_input();
    if (!boot.fb.pixels) {
        auth_tick(ticks / PIT_HZ);
        static const char *prompt, *status;
        if (prompt != auth_prompt() || status != auth_status()) {
            prompt = auth_prompt();
            status = auth_status();
            log_write("AUTH: ");
            log_write(status);
            log_write("\nAUTH PROMPT: ");
            log_write(prompt);
            log_write("\n");
        }
        static bool signed_in;
        if (auth_logged_in() && !signed_in)
            files_session();
        signed_in = auth_logged_in();
    }
    ui_tick();
    if (boot.selftest && ticks > 30) {
        bool done = true;
        for (struct cpu *c = &primary; c; c = c->next)
            if (!__atomic_load_n(&c->completed, __ATOMIC_ACQUIRE))
                done = false;
        if (done) {
            log_write("ARM BOOT PASS: ");
            log_dec(cpu_online_count());
            log_write(" CPUs, real EL0 faults, timer preemption, MNFS and account service\n");
            power_off();
        }
        if (ticks > 1500)
            panic("SMP selftest deadline");
    }
}
void pci_read_line(unsigned i, char *s, size_t n) {
    (void)i;
    const char *t = "ARM platform devices (FDT / VirtIO MMIO)";
    if (n) {
        size_t k = strlen(t);
        if (k >= n)
            k = n - 1;
        memcpy(s, t, k);
        s[k] = 0;
    }
}
bool graphics_available(void) {
    return boot.fb.pixels != NULL;
}
bool graphics_mode(unsigned w, unsigned h) {
    return w == boot.fb.width && h == boot.fb.height && boot.fb.pixels;
}
const char *graphics_driver(void) {
    return boot.fb.pixels ? "UEFI GOP / ARM64 (fixed boot mode)" : "PL011 serial console";
}
void arm_main(void) {
    __asm__ volatile("msr daifset,#15");
    primary.self = &primary;
    primary.hardware_id = sys_read(mpidr_el1) & 0xff00ffffffULL;
    primary.online = 1;
    sys_write(tpidr_el1, (uintptr_t)&primary);
    if (!platform.uart || !platform.gicd ||
        (platform.gic_version != 2 && platform.gic_version != 3))
        panic("PL011 / GIC platform description required");
    log_write("mnOS original ARM64 kernel / EL1\n");
    log_write(platform.uefi ? "UEFI ExitBootServices completed\n" : "Direct FDT boot\n");
    log_write(platform.fdt ? "ARM platform: FDT\n" : "ARM platform: ACPI\n");
    memory_init();
    primary.checks = 1;
    arm_irq_init();
    arm_irq_cpu();
    arm_virtio_init();
    files_init();
    ui_init();
    primary.irq_top = (uintptr_t)kernel_alloc(65536) + 65536;
    tasks_init_for(&primary);
    start_cpus();
    log_write("ARM: entering EL0 with 100 Hz timer and independent address spaces\n");
    arm_timer();
    extern void arm_enter(struct irq_frame *);
    struct irq_frame *f = (void *)(primary.irq_top - 288);
    memcpy(f, &tasks[0].frame, sizeof *f);
    sys_write(ttbr0_el1, tasks[0].cr3);
    barrier();
    arm_enter(f);
    __builtin_unreachable();
}
#ifndef _WIN32
bool arm_firmware_random(uint8_t *p, size_t n) {
    (void)p;
    (void)n;
    return false;
}
extern char image_start[], image_end[], text_end[], ro_start[], ro_end[];
_Noreturn void arm_start(void *fdt) {
    if (!arm_fdt(fdt))
        for (;;)
            __asm__ volatile("wfi");
    platform.image_start = (uintptr_t)image_start;
    platform.image_end = (uintptr_t)image_end;
    platform.text_start = (uintptr_t)image_start;
    platform.text_end = (uintptr_t)text_end;
    platform.ro_start = (uintptr_t)ro_start;
    platform.ro_end = (uintptr_t)ro_end;
    arm_main();
    __builtin_unreachable();
}
#endif
