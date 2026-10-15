/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_BLOCK_H
#define MNOS_BLOCK_H
#include "kernel.h"
struct block_device { uintptr_t transport; uint16_t port, control; uint8_t drive; bool lba48; uint64_t sectors; };
unsigned block_discover(struct block_device *, unsigned);
bool block_read(struct block_device *, uint64_t, unsigned, void *);
bool block_write(struct block_device *, uint64_t, unsigned, const void *);
bool block_flush(struct block_device *);
#endif
