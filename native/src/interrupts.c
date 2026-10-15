/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#define idle_wakes (cpu_this()->idle_wakes)
#define probe_counts (cpu_this()->probe_counts)
uint8_t irq_stack_area[PAGE + 65536] __attribute__((aligned(4096)));
static uint8_t fault_stack[16384] __attribute__((aligned(4096)));
extern uint64_t gdt64[];
extern void *isr_table[];
struct idt_entry {
    uint16_t low, selector;
    uint8_t ist, flags;
    uint16_t middle;
    uint32_t high, zero;
} __attribute__((packed));
static struct idt_entry idt[256] __attribute__((aligned(16)));
static void gate(unsigned n, uintptr_t target, unsigned flags, unsigned ist) {
    idt[n] = (struct idt_entry){target & 0xffff, 8, ist, flags, (target >> 16) & 0xffff,
                                target >> 32,    0};
}
static void io_wait(void) {
    outb(0x80, 0);
}
void interrupts_cpu_init(void) {
    struct cpu *cpu = cpu_this();
    struct tss64 *tss = &cpu->tss;
    if (!cpu->index) {
        cpu->irq_top = (uintptr_t)irq_stack_area + sizeof irq_stack_area;
        cpu->fault_top = (uintptr_t)fault_stack + sizeof fault_stack;
    }
    tss->rsp[0] = cpu->irq_top;
    tss->ist[0] = cpu->fault_top;
    tss->iomap = sizeof *tss;
    memcpy(cpu->gdt, gdt64, sizeof cpu->gdt);
    uint64_t base = (uintptr_t)tss, limit = sizeof *tss - 1;
    cpu->gdt[5] = (limit & 0xffff) | ((base & 0xffffff) << 16) | (0x89ULL << 40) |
                 ((limit & 0xf0000) << 32) | ((base & 0xff000000) << 32);
    cpu->gdt[6] = base >> 32;
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr =
        {sizeof cpu->gdt - 1, (uintptr_t)cpu->gdt}, idtr = {sizeof idt - 1, (uintptr_t)idt};
    __asm__ volatile("lgdt %0" :: "m"(gdtr));
    __asm__ volatile("ltr %0" :: "r"((uint16_t)0x28));
    __asm__ volatile("lidt %0" :: "m"(idtr));
}
void interrupts_init(void) {
    for (unsigned i = 0; i < 256; ++i)
        gate(i, (uintptr_t)isr_table[49], 0x8e, 0);
    for (unsigned i = 0; i < 48; ++i)
        gate(i, (uintptr_t)isr_table[i], 0x8e, i == 8 ? 1 : 0);
    gate(128, (uintptr_t)isr_table[48], 0xee, 0);
    gate(64, (uintptr_t)isr_table[50], 0x8e, 0);
    interrupts_cpu_init();
    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();
    outb(0x21, 4);
    io_wait();
    outb(0xa1, 2);
    io_wait();
    outb(0x21, 1);
    io_wait();
    outb(0xa1, 1);
    io_wait();
    outb(0x21, 0xf8);
    outb(0xa1, 0xef);
    uint16_t divisor = 1193182 / PIT_HZ;
    outb(0x43, 0x36);
    outb(0x40, divisor & 255);
    outb(0x40, divisor >> 8);
}
extern char user_loop[], user_loop_end[], user_read_kernel[], user_read_kernel_end[], user_nx[],
    user_nx_end[], user_guard[], user_guard_end[], user_pointer[], user_pointer_end[];
extern char user_write_code[], user_write_code_end[], user_port_io[], user_port_io_end[];
static void spawn(struct cpu *cpu, unsigned id, const void *program, size_t size, unsigned probe) {
    if (size > PAGE)
        panic("user program exceeds one code page");
    struct task *t = &cpu->work[id];
    t->code = page_alloc();
    t->data = page_alloc();
    t->stack = page_alloc();
    memcpy(phys_pointer(t->code), program, size);
    t->cr3 = address_space(t->code, t->data, t->stack);
    t->frame = (struct irq_frame){
        .rip = USER_BASE, .cs = 0x1b, .ss = 0x23, .rflags = 0x202, .rsp = USER_STACK - 16};
    t->ready = true;
    t->probe = probe;
}
void tasks_init_for(struct cpu *cpu) {
    spawn(cpu, 0, user_loop, user_loop_end - user_loop, 0);
    spawn(cpu, 1, user_loop, user_loop_end - user_loop, 0);
    spawn(cpu, 2, user_read_kernel, user_read_kernel_end - user_read_kernel, 1);
    spawn(cpu, 3, user_nx, user_nx_end - user_nx, 2);
    spawn(cpu, 4, user_guard, user_guard_end - user_guard, 3);
    spawn(cpu, 5, user_pointer, user_pointer_end - user_pointer, 4);
    spawn(cpu, 6, user_write_code, user_write_code_end - user_write_code, 5);
    spawn(cpu, 7, user_port_io, user_port_io_end - user_port_io, 6);
}
void tasks_init(void) { tasks_init_for(cpu_this()); }
static struct irq_frame *schedule(struct irq_frame *frame) {
    if ((frame->cs & 3) != 3)
        return frame;
    tasks[current_task].frame = *frame;
    for (;;) {
        bool sleeping = false;
        for (unsigned n = 1; n <= MAX_TASKS; ++n) {
            unsigned id = (current_task + n) % MAX_TASKS;
            if (tasks[id].ready) {
                current_task = id;
                ++tasks[id].switches;
                cr3_set(tasks[id].cr3);
                return &tasks[id].frame;
            }
            if (tasks[id].wake_tick)
                sleeping = true;
        }
        if (!sleeping)
            panic("no runnable user tasks");
        /* Idle on the kernel IRQ stack. Nested kernel IRQs never switch tasks. */
        __asm__ volatile("sti; hlt; cli" ::: "memory");
        ++idle_wakes;
    }
}
static const uint8_t *user_read(struct task *t, uint64_t ptr, uint64_t len) {
    if (len > 256 || ptr < USER_BASE || ptr >= USER_STACK)
        return NULL;
    uint64_t page = (ptr - USER_BASE) / PAGE, offset = ptr & (PAGE - 1);
    if (len > PAGE - offset || page == 2)
        return NULL;
    uint64_t physical = page == 0 ? t->code : page == 1 ? t->data : page == 3 ? t->stack : 0;
    return physical ? phys_pointer(physical + offset) : NULL;
}
static void pass(const char *what) {
    ++test_passed;
    if (!cpu_this()->index) {
        log_write("PASS: "); log_write(what); log_write("\n");
    }
}
static struct irq_frame *syscall(struct irq_frame *f) {
    struct task *t = &tasks[current_task];
    switch (f->rax) {
    case 0: {
        const uint8_t *s = user_read(t, f->rdi, f->rsi);
        if (!s) {
            f->rax = (uint64_t)-14;
            if (t->probe == 4 && f->rdi == 0x100000 && f->rsi == 16)
                t->pointer_checked = true;
            break;
        }
        for (uint64_t i = 0; i < f->rsi; ++i) {
            char text[2] = {(s[i] >= 32 && s[i] < 127) || s[i] == '\n' ? (char)s[i] : '.', 0};
            log_write(text);
        }
        f->rax = f->rsi;
        break;
    }
    case 1:
        f->rax = 0;
        return schedule(f);
    case 2:
        f->rax = ticks;
        break;
    case 3:
        t->ready = false;
        return schedule(f);
    case 4:
        if (t->probe == 4 && t->pointer_checked) {
            pass("syscall rejects supervisor pointers");
            t->probe = 0;
        }
        f->rax = 0;
        break;
    case 5:
        if (f->rdi < 1 || f->rdi > 1000) {
            f->rax = (uint64_t)-22;
            break;
        }
        t->wake_tick = ticks + f->rdi;
        t->ready = false;
        f->rax = 0;
        return schedule(f);
    default:
        f->rax = (uint64_t)-38;
        break;
    }
    return f;
}
struct irq_frame *irq_dispatch(struct irq_frame *f) {
    if (cpu_smap)
        __asm__ volatile("clac" ::: "memory");
    if (f->vector < 32) {
        if ((f->cs & 3) == 3) {
            struct task *t = &tasks[current_task];
            uint64_t addr = read_cr2();
            if (f->vector == 14 && t->probe == 1 && addr == 0x100000 && (f->error & 5) == 5)
                pass("ring 3 cannot read supervisor memory");
            else if (f->vector == 14 && t->probe == 2 && addr == USER_DATA &&
                     (f->error & 0x15) == 0x15)
                pass("user data page is non-executable");
            else if (f->vector == 14 && t->probe == 3 && addr == USER_BASE + 2 * PAGE &&
                     (f->error & 7) == 6)
                pass("user stack guard is unmapped");
            else if (f->vector == 14 && t->probe == 5 && addr == USER_BASE && (f->error & 7) == 7)
                pass("user code page is read-only");
            else if (f->vector == 13 && t->probe == 6)
                pass("ring 3 cannot access I/O ports");
            else {
                log_write("Unexpected user fault vector=");
                log_dec(f->vector);
                log_write(" address=");
                log_hex(addr);
                panic("user isolation test failed");
            }
            t->ready = false;
            return schedule(f);
        }
        log_write("Kernel exception vector=");
        log_dec(f->vector);
        log_write(" rip=");
        log_hex(f->rip);
        log_write(" cr2=");
        log_hex(read_cr2());
        log_write(" error=");
        log_hex(f->error);
        panic("kernel exception");
    }
    if (f->vector == 128)
        return syscall(f);
    if (f->vector == 33 || f->vector == 44) {
        unsigned count = 32;
        while (count--) {
            uint8_t status = inb(0x64);
            if (!(status & 1))
                break;
            uint8_t data = inb(0x60);
            if (status & 0x20)
                mouse_event(data);
            else
                keyboard_event(data);
        }
    }
    if (f->vector == 32 || f->vector == 64) {
        ++ticks;
        for (unsigned i = 0; i < MAX_TASKS; ++i)
            if (tasks[i].wake_tick && tasks[i].wake_tick <= ticks) {
                tasks[i].wake_tick = 0;
                tasks[i].ready = true;
            }
        if (!cpu_this()->index) {
            if (inb(0x3fd) & 1) ui_key((char)inb(0x3f8));
            ui_tick();
        }
        if (ticks == 30) {
            if (test_passed != SELFTEST_COUNT - 1 || !*(uint64_t *)phys_pointer(tasks[0].data) ||
                !*(uint64_t *)phys_pointer(tasks[1].data) || tasks[0].data == tasks[1].data ||
                !tasks[0].switches || !tasks[1].switches)
                panic("scheduler/probe completion test failed");
            pass("timer preempts two isolated ring-3 address spaces");
            /* After the preemption probe, workers sleep; idle uses HLT. */
            *(uint64_t *)phys_pointer(tasks[0].data + 8) = 1;
            *(uint64_t *)phys_pointer(tasks[1].data + 8) = 1;
            probe_counts[0] = *(uint64_t *)phys_pointer(tasks[0].data);
            probe_counts[1] = *(uint64_t *)phys_pointer(tasks[1].data);
        }
        if (ticks == 60) {
            if (!idle_wakes || *(uint64_t *)phys_pointer(tasks[0].data) <= probe_counts[0] ||
                *(uint64_t *)phys_pointer(tasks[1].data) <= probe_counts[1])
                panic("sleep/wakeup and idle test failed");
            __atomic_store_n(&cpu_this()->completed, 1, __ATOMIC_RELEASE);
        }
        if (!cpu_this()->index) smp_tick_complete();
    }
    if (f->vector == 64) lapic_eoi();
    if (f->vector >= 40 && f->vector < 48)
        outb(0xa0, 0x20);
    if (f->vector >= 32 && f->vector < 48)
        outb(0x20, 0x20);
    return f->vector == 32 || f->vector == 64 ? schedule(f) : f;
}
