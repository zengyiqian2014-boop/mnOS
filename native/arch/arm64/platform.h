/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_ARM_PLATFORM_H
#define MNOS_ARM_PLATFORM_H
#include "kernel.h"
#define ARM_RANGES 128
#define ARM_VIRTIO 64
struct arm_range {
    uint64_t start, size;
};
struct arm_platform {
    const void *fdt;
    size_t fdt_size;
    struct arm_range ram[ARM_RANGES], reserved[ARM_RANGES];
    unsigned ram_count, reserved_count;
    uintptr_t uart, gicd, gicc, gicr;
    uint64_t gicr_size;
    unsigned gic_version, timer_irq;
    bool psci_hvc, uefi;
    uintptr_t virtio[ARM_VIRTIO];
    unsigned virtio_count;
    uintptr_t image_start, image_end, text_start, text_end, ro_start, ro_end;
};
extern struct arm_platform platform;
extern uint64_t arm_root, arm_tcr;
extern void arm_vectors(void), arm_user_start(void), arm_user_end(void);
extern void arm_secondary(void);
bool arm_fdt(const void *);
bool arm_acpi(const void *, bool (*)(uint64_t, size_t));
void arm_acpi_cpus(void (*)(uint64_t));
bool arm_firmware_random(uint8_t *, size_t);
void arm_fdt_cpus(void (*)(uint64_t));
void arm_main(void);
_Noreturn void arm_start(void *);
_Noreturn void arm_ap_main(struct cpu *);
void arm_mmu_on(uint64_t);
void arm_cache_code(uintptr_t, size_t);
void arm_map(uint64_t *, uint64_t, uint64_t, uint64_t);
void arm_irq_init(void);
void arm_irq_cpu(void);
void arm_timer(void);
struct irq_frame *arm_exception(struct irq_frame *, uint64_t);
void arm_input_poll(void);
int64_t arm_psci(uint64_t, uint64_t, uint64_t, uint64_t);
void arm_virtio_init(void);
void arm_virtio_input(void);
#endif
