/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_BLOCK_H
#define MNOS_BLOCK_H
#include "kernel.h"
struct block_device { uint16_t port, control; uint8_t drive; bool lba48; uint64_t sectors; };
unsigned ata_discover(struct block_device *, unsigned);
bool ata_read(struct block_device *, uint64_t, unsigned, void *);
bool ata_write(struct block_device *, uint64_t, unsigned, const void *);
bool ata_flush(struct block_device *);
#endif
