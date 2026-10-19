/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_USERCOPY_H
#define MNOS_USERCOPY_H
#include <stdint.h>
#include <stdbool.h>
struct task;
/* Kernel direct-map alias of one validated user range. Writable requests never
 * include the executable page. Caller holds the process context with IRQs off. */
void *user_buffer(const struct task *, uint64_t, uint64_t, bool writable);
int64_t file_request(const struct task *, uint32_t uid, uint64_t, uint64_t);
#endif
