/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_GUEST_H
#define MNOS_GUEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Original single-hart RV64IMAC/Sv39 interpreter. Guest addresses are never
 * host pointers. No host files, disk, network, syscalls or DMA are forwarded. */
#define GUEST_BASE UINT64_C(0x80000000)
#define GUEST_RAM (64u * 1024u * 1024u)
#define GUEST_LOAD (2u * 1024u * 1024u)
#define GUEST_DTB (GUEST_RAM - 65536u)
struct guest_tlb { uint64_t tag, physical; unsigned valid; };
struct guest {
    uint64_t x[32], pc, retired, time, deadline, reservation;
    uint64_t status, ie, ip, vector, scratch, epc, cause, tval, satp, counteren;
    uint8_t *ram;
    size_t ram_size;
    unsigned privilege, stopped, waiting;
    struct guest_tlb tlb[256];
    int (*input)(void *);
    void (*output)(void *, unsigned char);
    void *opaque;
};
void guest_init(struct guest *, uint8_t *, size_t);
/* Runs at most budget instructions. A stopped guest does no further work. */
void guest_run(struct guest *, unsigned budget);
bool guest_copy(struct guest *, uint64_t physical, const void *, size_t);
void guest_flush(struct guest *);
#endif
