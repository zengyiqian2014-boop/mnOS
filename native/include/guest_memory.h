/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_GUEST_MEMORY_H
#define MNOS_GUEST_MEMORY_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Sparse physical pages owned by one guest. Three radix levels cover 512 GiB;
 * the configured RAM size and available host pages impose smaller limits. */
struct guest_memory {
    uint64_t root, bytes, pages, budget;
    bool failed;
    uint64_t (*allocate)(void);
    void *(*pointer)(uint64_t);
    void (*release)(uint64_t);
};
void *guest_memory_page(void *, uint64_t offset);
void guest_memory_clear(struct guest_memory *);
/* Validate a flattened device tree and change its memory node's RAM extent. */
bool guest_dtb_memory(void *, size_t, uint64_t);
#endif
