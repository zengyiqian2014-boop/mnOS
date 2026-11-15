/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_GUEST_H
#define MNOS_GUEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Original single-hart RV64GC/Sv39 interpreter. Guest addresses are never
 * host pointers. Optional SBI devices expose bounded private storage, Ethernet,
 * input and entropy; guest syscalls and DMA never run on the host directly. */
#define GUEST_BASE UINT64_C(0x80000000)
#define GUEST_RAM (64u * 1024u * 1024u)
#define GUEST_LOAD (2u * 1024u * 1024u)
#define GUEST_DTB (GUEST_RAM - 65536u)
struct guest_tlb { uint64_t tag, physical; uint8_t *pointer; unsigned valid; };
struct guest {
    uint64_t x[32], pc, retired, time, deadline, reservation;
    uint64_t f[32];
    unsigned fcsr;
    uint64_t status, ie, ip, vector, scratch, epc, cause, tval, satp, counteren;
    uint8_t *ram;
    size_t ram_size;
    /* A host may supply sparse pages instead of one permanent allocation.
     * The callback returns a page base for a bounded guest RAM offset. */
    void *(*ram_page)(void *, uint64_t);
    void *ram_context;
    unsigned privilege, stopped, waiting, yield_on_idle, external_time;
    struct guest_tlb tlb[256];
    int (*input)(void *);
    void (*output)(void *, unsigned char);
    /* Supervisor-only vendor SBI transport; callback receives guest physical
     * addresses, never unvalidated host pointers. */
    int64_t (*device)(struct guest *, unsigned, const uint64_t *, uint64_t *);
    void *opaque;
};
void guest_init(struct guest *, uint8_t *, size_t);
/* Runs at most budget instructions. A stopped guest does no further work. */
void guest_run(struct guest *, unsigned budget);
bool guest_copy(struct guest *, uint64_t physical, const void *, size_t);
bool guest_read(struct guest *, uint64_t physical, void *, size_t);
void guest_flush(struct guest *);
#endif
