/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_CSPRNG_H
#define MNOS_CSPRNG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Caller serializes access. Seed only from a trusted firmware/CPU/device RNG. */
struct csprng {uint8_t key[32];bool ready;};
bool csprng_seed(struct csprng *,const uint8_t [32]);
bool csprng_read(struct csprng *,uint8_t *,size_t);
#endif
