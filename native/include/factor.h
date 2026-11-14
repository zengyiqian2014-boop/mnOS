/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_FACTOR_H
#define MNOS_FACTOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FACTOR_RECOVERY 8
/* Secret: 256 random bits; RFC 6238 HMAC-SHA1, 6 digits, 30 seconds.
 * All integers are authenticated in a fixed little-endian header. */
struct factor_record {
    uint32_t enabled;
    uint8_t salt[16], nonce[24], tag[16], ciphertext[32];
    uint64_t last_step;
    uint8_t recovery[FACTOR_RECOVERY][32];
};
typedef bool (*factor_random)(uint8_t *,size_t);
uint32_t factor_totp(const uint8_t [32],uint64_t,unsigned);
uint32_t factor_hotp(const void *,size_t,uint64_t,unsigned);
void factor_base32(const uint8_t [32],char [53]);
bool factor_open(const struct factor_record *,uint32_t,const uint8_t [32],uint8_t [32]);
bool factor_seal(struct factor_record *,uint32_t,const uint8_t [32],const uint8_t [32],factor_random);
/* Returns accepted timestep; no mutation. Persistent replay state must be saved
 * before the caller grants access. Does not allow timestep 0 or clock rollback. */
bool factor_check(const uint8_t [32],uint64_t,uint64_t,const char *,uint64_t *);
void factor_recovery_hash(const char *,const uint8_t [16],uint8_t [32]);
bool factor_same(const uint8_t *,const uint8_t *,size_t);
#endif
