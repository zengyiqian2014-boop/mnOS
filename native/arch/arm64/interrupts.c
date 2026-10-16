/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform.h"
#include "process.h"
static void wait_clear(uintptr_t p, uint32_t mask) {
    for (unsigned n = 0; n < 10000000; ++n)
        if (!(mmio_read(p) & mask))
            return;
    panic("GIC register timeout");
}
void arm_irq_init(void) {
    mmio_write(platform.gicd, 0);
    if (platform.gic_version == 3)
        wait_clear(platform.gicd, 1u << 31);
    mmio_write(platform.gicd, platform.gic_version == 3 ? 0x12 : 3);
    barrier();
}
void arm_irq_cpu(void) {
    uintptr_t bank = platform.gicd;
    if (platform.gic_version == 3) {
        uintptr_t r = platform.gicr;
        uint64_t mp = sys_read(mpidr_el1);
        uint32_t affinity = ((mp >> 32) & 255) << 24 | (mp & 0xffffff);
        for (;;) {
            uint64_t type = *(volatile uint64_t *)(r + 8);
            if ((type >> 32) == affinity)
                break;
            if ((type & 16) || r - platform.gicr + 0x20000 >= platform.gicr_size)
                panic("GIC redistributor missing");
            r += (type & 2) ? 0x40000 : 0x20000;
        }
        mmio_write(r + 0x14, mmio_read(r + 0x14) & ~2u);
        wait_clear(r + 0x14, 4);
        bank = r + 0x10000;
        sys_write(icc_sre_el1, 1);
        barrier();
        sys_write(icc_pmr_el1, 255);
        sys_write(icc_bpr1_el1, 0);
        sys_write(icc_igrpen1_el1, 1);
    } else {
        mmio_write(platform.gicc + 4, 255);
        mmio_write(platform.gicc + 8, 0);
        mmio_write(platform.gicc, 3);
    }
    mmio_write(bank + 0x80, platform.gic_version == 3 ? 0xffffffff : 0);
    mmio_write(bank + 0x180, 0xffffffff);
    mmio_write(bank + 0x400 + (platform.timer_irq & ~3u), 0x80808080);
    mmio_write(bank + 0x100, 1u << platform.timer_irq);
    barrier();
}
void arm_timer(void) {
    uint64_t rate = sys_read(cntfrq_el0);
    if (rate < 100)
        panic("counter frequency");
    sys_write(cntp_tval_el0, rate / PIT_HZ);
    sys_write(cntp_ctl_el0, 1);
    barrier();
}
static uint32_t ack(void) {
    return platform.gic_version == 3 ? sys_read(icc_iar1_el1) : mmio_read(platform.gicc + 12);
}
static void eoi(uint32_t n) {
    if (platform.gic_version == 3)
        sys_write(icc_eoir1_el1, n);
    else
        mmio_write(platform.gicc + 16, n);
    barrier();
}
#include "exec.h"
extern char native_exec_start[], native_exec_end[];
void tasks_init_for(struct cpu *c) {
    size_t size = (uintptr_t)arm_user_end - (uintptr_t)arm_user_start;
    if (size > PAGE)
        panic("EL0 probe size");
    for (unsigned i = 0; i < PROBE_TASKS; ++i) {
        struct task *t = &c->work[i];
        t->code = page_alloc();
        t->data = page_alloc();
        t->stack = page_alloc();
        uint64_t entry;
        if (elf_load(native_exec_start, native_exec_end - native_exec_start, 183, USER_BASE,
                     (void *)t->code, (void *)t->data, &entry))
            panic("invalid native ARM ELF executable");
        arm_cache_code(t->code, size);
        t->cr3 = address_space(t->code, t->data, t->stack);
        t->ready = true;
        t->probe = i;
        t->frame.x[0] = i;
        t->frame.x[1] = USER_DATA;
        t->frame.x[2] = (uintptr_t)&platform;
        t->frame.pc = entry;
        t->frame.sp = USER_STACK + PAGE;
        t->frame.psr = 0;
    }
    if (!c->index) log_write("Native ELF64 ARM64 loaded: independent user executable\n");
}
static void select(struct irq_frame *f) {
    tasks[current_task].frame = *f;
    for (;;) {
        for (unsigned n = 1; n <= MAX_TASKS; ++n) {
            unsigned i = (current_task + n) % MAX_TASKS;
            if (tasks[i].ready && tasks[i].wake_tick <= ticks) {
                current_task = i;
                ++tasks[i].switches;
                *f = tasks[i].frame;
                sys_write(ttbr0_el1, tasks[i].cr3);
                __asm__ volatile("dsb ish; tlbi vmalle1; dsb ish; isb" ::: "memory");
                process_reap();
                return;
            }
        }
        ++cpu_this()->idle_wakes;
        __asm__ volatile("msr daifclr,#2; wfi; msr daifset,#2" ::: "memory");
    }
}
struct irq_frame *arm_exception(struct irq_frame *f, uint64_t vector) {
    uint64_t esr = sys_read(esr_el1), far = sys_read(far_el1);
    unsigned ec = esr >> 26;
    bool user = (f->psr & 15) == 0;
    if (vector % 4 == 1) {
        uint32_t id = ack();
        if ((id & 0xffffff) == platform.timer_irq) {
            arm_timer();
            ++ticks;
            process_tick();
            if (user && current_task < 2) {
                uint64_t count = *(uint64_t *)tasks[current_task].data;
                if (current_task < 2 && count > cpu_this()->probe_counts[current_task])
                    cpu_this()->probe_counts[current_task] = count;
                if (!cpu_this()->preemption_checked && tasks[0].switches > 2 &&
                    tasks[1].switches > 2 && cpu_this()->probe_counts[0] &&
                    cpu_this()->probe_counts[1]) {
                    cpu_this()->preemption_checked = true;
                    ++test_passed;
                    log_write("ARM timer preemption PASS\n");
                }
            }
            eoi(id);
            if (cpu_this()->index == 0)
                arm_input_poll();
            smp_tick_complete();
            if (user)
                select(f);
        } else if (id < 1020)
            eoi(id);
        return f;
    }
    if (user && ec == 0x15) {
        if (current_task == APP_TASK) {
            bool reschedule;
            f->x[0] = process_syscall(f->x[8], f->x[0], f->x[1], &reschedule);
            if (reschedule) select(f);
            return f;
        }
        uint64_t call = f->x[8];
        if (call == 0) { /* No arbitrary user dereference: mapped data page only. */
            if (f->x[0] < USER_DATA || f->x[0] >= USER_DATA + PAGE)
                f->x[0] = UINT64_MAX;
            else
                f->x[0] = 0;
        } else if (call == 4 && current_task == 5) {
            ++test_passed;
            tasks[5].ready = false;
            log_write("ARM syscall pointer rejection PASS\n");
            select(f);
        } else if (call == 5 && current_task < 2) {
            tasks[current_task].wake_tick = ticks + (f->x[0] > 100 ? 100 : f->x[0]);
            select(f);
        } else
            panic("EL0 probe escaped or unknown syscall");
        return f;
    }
    if (user) {
        if (current_task == APP_TASK) {
            process_exit(128 + (int)ec, "fault");
            select(f);
            return f;
        }
        unsigned i = current_task;
        unsigned dfsc = esr & 63;
        bool translation = dfsc >= 4 && dfsc <= 7, permission = dfsc >= 12 && dfsc <= 15;
        extern void arm_user_privileged(void);
        bool ok = (i == 2 && ec == 0x24 && permission && far == (uintptr_t)&platform) ||
                  (i == 3 && ec == 0x20 && permission && far == USER_DATA) ||
                  (i == 4 && ec == 0x24 && translation && far == USER_STACK - 8) ||
                  (i == 6 && ec == 0x24 && permission && (esr & (1u << 6)) && far == USER_BASE) ||
                  (i == 7 && (ec == 0 || ec == 0x18) &&
                   f->pc == USER_BASE + (uintptr_t)arm_user_privileged - (uintptr_t)arm_user_start);
        if (ok) {
            char line[64] = "ARM EL0 fault probe PASS ";
            number(line + 25, i);
            size_t at = strlen(line);
            line[at++] = '\n';
            line[at] = 0;
            log_write(line);
            ++test_passed;
            tasks[i].ready = false;
            select(f);
            return f;
        }
    }
    log_write("ARM exception vector=");
    log_dec(vector);
    log_write(" ESR=");
    log_hex(esr);
    log_write(" FAR=");
    log_hex(far);
    log_write(" PC=");
    log_hex(f->pc);
    log_write("\n");
    panic("unexpected ARM exception");
}
