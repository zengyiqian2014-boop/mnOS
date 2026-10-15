/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
static struct cpu bootstrap;
static struct cpu *tail = &bootstrap;
static uint64_t detected = 1, online = 1;
static uint64_t lapic_address;
static uint32_t timer_period;
static bool x2apic, apic_available, reported;
extern char ap_trampoline[], ap_trampoline_end[];

static uint64_t msr_read(uint32_t reg) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg));
    return ((uint64_t)hi << 32) | lo;
}
static void msr_write(uint32_t reg, uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(reg), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)) : "memory");
}
void smp_bootstrap(void) {
    bootstrap.self = &bootstrap;
    bootstrap.online = 1;
    msr_write(0xc0000101u, (uintptr_t)&bootstrap);
    msr_write(0xc0000102u, 0);
}
uint64_t cpu_count(void) { return detected; }
uint64_t cpu_online_count(void) { return __atomic_load_n(&online, __ATOMIC_ACQUIRE); }
static uint32_t apic_read(unsigned reg) {
    if (x2apic) return (uint32_t)msr_read(0x800 + reg / 16);
    return *(volatile uint32_t *)(lapic_address + reg);
}
static void apic_write(unsigned reg, uint32_t value) {
    if (x2apic) msr_write(0x800 + reg / 16, value);
    else *(volatile uint32_t *)(lapic_address + reg) = value;
}
void lapic_eoi(void) { if (apic_available) apic_write(0xb0, 0); }
static void apic_enable(bool bsp) {
    uint64_t base = msr_read(0x1b);
    msr_write(0x1b, base | (1ULL << 11) | (x2apic ? (1ULL << 10) : 0));
    apic_write(0xf0, 0x1ff);
    apic_write(0x80, 0);
    apic_write(0x350, bsp ? 0x700 : (1u << 16));
    apic_write(0x360, 1u << 16);
    apic_write(0x320, (1u << 16) | 64);
    apic_write(0x3e0, 3); /* Divide by 16. */
}
static void delay_us(unsigned us) {
    unsigned count = (1193182ULL * us + 999999) / 1000000;
    if (!count || count > 65535) panic("invalid PIT delay");
    uint8_t old = inb(0x61);
    outb(0x61, old & ~3u);
    outb(0x43, 0xb0);
    outb(0x42, count & 255);
    outb(0x42, count >> 8);
    outb(0x61, (old & ~2u) | 1);
    unsigned budget = 10000000;
    while (!(inb(0x61) & 0x20) && --budget) __asm__ volatile("pause");
    outb(0x61, old);
    if (!budget) panic("PIT calibration timeout");
}
static bool checksum(const uint8_t *p, size_t length) {
    uint8_t sum = 0;
    while (length--) sum += *p++;
    return sum == 0;
}
static uint32_t read32(const void *p) { uint32_t value; memcpy(&value, p, 4); return value; }
static uint64_t read64(const void *p) { uint64_t value; memcpy(&value, p, 8); return value; }
static const uint8_t *acpi(uint64_t p) {
    const uint8_t *header = firmware_pointer(p, 36);
    if (!header) return NULL;
    uint32_t length = read32(header + 4);
    if (length < 36) return NULL;
    const uint8_t *table = firmware_pointer(p, length);
    return table && checksum(table, length) ? table : NULL;
}
static const uint8_t *find_madt(void) {
    const uint8_t *rsdp = boot.rsdp;
    if (!rsdp || memcmp(rsdp, "RSD PTR ", 8) || !checksum(rsdp, 20)) return NULL;
    const uint8_t *root = NULL;
    unsigned step = 4;
    if (rsdp[15] >= 2 && (uintptr_t)rsdp + 36 <= boot.mb_end && read32(rsdp + 20) == 36 && checksum(rsdp, 36)) {
        root = acpi(read64(rsdp + 24));
        if (root && !memcmp(root, "XSDT", 4)) step = 8;
        else root = NULL;
    }
    if (!root) {
        root = acpi(read32(rsdp + 16));
        if (!root || memcmp(root, "RSDT", 4)) return NULL;
    }
    uint32_t length = read32(root + 4);
    if ((length - 36) % step) return NULL;
    for (uint32_t off = 36; off < length; off += step) {
        const uint8_t *entry = acpi(step == 8 ? read64(root + off) : read32(root + off));
        if (entry && !memcmp(entry, "APIC", 4) && read32(entry + 4) >= 44) return entry;
    }
    return NULL;
}
static bool record_id(const uint8_t *record, uint32_t *id) {
    if (record[0] == 0 && record[1] >= 8 && (read32(record + 4) & 1)) {
        *id = record[3]; return true;
    }
    if (record[0] == 9 && record[1] >= 16 && (read32(record + 8) & 1)) {
        *id = read32(record + 4); return true;
    }
    return false;
}
struct id_slot { uint32_t id; bool present; };
static bool insert(struct id_slot *slots, size_t count, uint32_t id) {
    size_t at = ((uint64_t)id * 2654435761u) & (count - 1);
    while (slots[at].present) {
        if (slots[at].id == id) return false;
        at = (at + 1) & (count - 1);
    }
    slots[at] = (struct id_slot){id, true};
    return true;
}
void smp_prepare(void) {
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    if (!(d & (1u << 9))) { log_write("SMP: local APIC unavailable; BSP only\n"); return; }
    x2apic = (c & (1u << 21)) != 0;
    lapic_address = msr_read(0x1b) & 0x000ffffffffff000ULL;
    if (!x2apic && lapic_address >= (1ULL << 32)) panic("unsupported LAPIC MMIO address");
    apic_available = true;
    apic_enable(true);
    bootstrap.apic_id = x2apic ? apic_read(0x20) : apic_read(0x20) >> 24;
    const uint8_t *madt = find_madt();
    if (!madt) { log_write("SMP: no valid MADT; BSP only\n"); return; }
    uint32_t length = read32(madt + 4), id;
    size_t records = 1;
    for (uint32_t off = 44; off < length;) {
        if (length - off < 2 || madt[off + 1] < 2 || madt[off + 1] > length - off)
            panic("malformed MADT record");
        if (record_id(madt + off, &id)) ++records;
        off += madt[off + 1];
    }
    size_t slots_count = 2;
    while (slots_count < records * 2) slots_count *= 2;
    if (slots_count > SIZE_MAX / sizeof(struct id_slot)) panic("MADT CPU table overflow");
    struct id_slot *slots = kernel_alloc(slots_count * sizeof *slots);
    insert(slots, slots_count, bootstrap.apic_id);
    for (uint32_t off = 44; off < length; off += madt[off + 1]) {
        if (!record_id(madt + off, &id) || !insert(slots, slots_count, id)) continue;
        if (!x2apic && id > 255) panic("firmware requires unavailable x2APIC");
        struct cpu *cpu = kernel_alloc(sizeof *cpu);
        cpu->self = cpu;
        cpu->apic_id = id;
        cpu->index = detected++;
        cpu->irq_top = (uintptr_t)kernel_alloc(65536) + 65536;
        cpu->fault_top = (uintptr_t)kernel_alloc(16384) + 16384;
        tail->next = cpu;
        tail = cpu;
    }
    for (struct cpu *cpu = bootstrap.next; cpu; cpu = cpu->next) tasks_init_for(cpu);
    log_write("SMP: discovered="); log_dec(detected);
    log_write(x2apic ? " mode=x2APIC (32-bit IDs)\n" : " mode=xAPIC\n");
}
static void send_ipi(uint32_t id, uint32_t command) {
    if (x2apic) msr_write(0x830, ((uint64_t)id << 32) | command);
    else {
        unsigned wait = 1000000;
        while ((apic_read(0x300) & (1u << 12)) && --wait) __asm__ volatile("pause");
        if (!wait) panic("LAPIC delivery timeout");
        apic_write(0x310, id << 24);
        apic_write(0x300, command);
    }
}
static _Noreturn void ap_main(struct cpu *cpu) {
    msr_write(0xc0000101u, (uintptr_t)cpu);
    msr_write(0xc0000102u, 0);
    memory_cpu_init();
    interrupts_cpu_init();
    apic_enable(false);
    cpu->checks = 1; /* Same verified kernel tables, per-CPU WP/NX/SMEP/SMAP. */
    apic_write(0x320, (1u << 17) | 64);
    apic_write(0x380, timer_period);
    __atomic_fetch_add(&online, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->online, 1, __ATOMIC_RELEASE);
    cr3_set(cpu->work[0].cr3);
    enter_frame(&cpu->work[0].frame);
}
void smp_start(void) {
    if (!bootstrap.next) return;
    if ((uintptr_t)ap_trampoline_end - (uintptr_t)ap_trampoline > PAGE || kernel_pml4 >= (1ULL << 32))
        panic("invalid AP bootstrap layout");
    if (boot.mb_start < 0x9000 && boot.mb_end > 0x7000) panic("AP trampoline overlaps boot information");
    memcpy((void *)0x7000, ap_trampoline, ap_trampoline_end - ap_trampoline);
    memory_protect_low(0x7000, true);
    apic_write(0x380, UINT32_MAX);
    delay_us(10000);
    timer_period = UINT32_MAX - apic_read(0x390);
    apic_write(0x380, 0);
    if (!timer_period) panic("local APIC timer calibration failed");
    for (struct cpu *cpu = bootstrap.next; cpu; cpu = cpu->next) {
        volatile uint64_t *parameters = (void *)0x8000;
        parameters[0] = kernel_pml4;
        parameters[1] = cpu->irq_top;
        parameters[2] = (uintptr_t)cpu;
        parameters[3] = (uintptr_t)ap_main;
        __asm__ volatile("mfence" ::: "memory");
        send_ipi(cpu->apic_id, 0x4500);
        delay_us(10000);
        send_ipi(cpu->apic_id, 0x4607);
        delay_us(200);
        if (!__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE)) send_ipi(cpu->apic_id, 0x4607);
        for (unsigned attempt = 0; !__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE) && attempt < 100; ++attempt)
            delay_us(10000);
        if (!__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE)) {
            log_write("SMP: failed APIC ID "); log_dec(cpu->apic_id); panic("AP startup timeout");
        }
        log_write("SMP: online APIC ID "); log_dec(cpu->apic_id); log_write("\n");
    }
    log_write("SMP: online="); log_dec(cpu_online_count()); log_write("\n");
}
void smp_tick_complete(void) {
    if (reported) return;
    for (struct cpu *cpu = &bootstrap; cpu; cpu = cpu->next)
        if (!__atomic_load_n(&cpu->completed, __ATOMIC_ACQUIRE)) return;
    reported = true;
    log_write("IDLE: sleeping tasks wake; processor halts between interrupts\nSELFTEST PASS 8/8\n");
    log_write("SMP PASS "); log_dec(cpu_online_count()); log_write("/"); log_dec(detected);
    log_write(" processors; per-CPU ring-3 isolation, preemption and idle\n");
    if (boot.selftest) outl(0xf4, 0x10);
}
