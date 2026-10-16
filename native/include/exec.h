/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_EXEC_H
#define MNOS_EXEC_H
#include <stdint.h>
#include <stddef.h>
/* Initial native ABI: one RX code page, one RW/NX data page.
 * Destinations must be disjoint, kernel-owned pages, not aliases of image.
 * Failure leaves destinations and entry unchanged. No dynamic linking. */
int elf_load(const void *image, size_t size, uint16_t machine, uint64_t base,
             void *code, void *data, uint64_t *entry);
#endif
