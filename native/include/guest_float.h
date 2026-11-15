/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_GUEST_FLOAT_H
#define MNOS_GUEST_FLOAT_H
#include <stdbool.h>
#include <stdint.h>
/* Integer-only IEEE binary32/binary64 for the software CPU. No host FPU state. */
enum { GF_NX=1, GF_UF=2, GF_OF=4, GF_DZ=8, GF_NV=16 };
struct guest_float { uint64_t bits; unsigned flags; };
struct guest_float guest_float_add(uint64_t, uint64_t, unsigned, unsigned);
struct guest_float guest_float_mul(uint64_t, uint64_t, unsigned, unsigned);
struct guest_float guest_float_div(uint64_t, uint64_t, unsigned, unsigned);
struct guest_float guest_float_sqrt(uint64_t, unsigned, unsigned);
struct guest_float guest_float_fma(uint64_t, uint64_t, uint64_t, unsigned, unsigned);
struct guest_float guest_float_convert(uint64_t, unsigned, unsigned, unsigned);
struct guest_float guest_float_from_int(uint64_t, bool, unsigned, unsigned, unsigned);
struct guest_float guest_float_to_int(uint64_t, bool, unsigned, unsigned, unsigned);
unsigned guest_float_class(uint64_t, unsigned);
bool guest_float_less(uint64_t, uint64_t, unsigned);
#endif
