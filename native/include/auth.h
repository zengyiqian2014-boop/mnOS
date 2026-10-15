/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_AUTH_H
#define MNOS_AUTH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AUTH_USERS 16
#define AUTH_ROUNDS 600000u
struct auth_user { uint32_t uid; char name[32]; uint8_t salt[16], hash[32]; };
struct auth_database { uint32_t magic, version, rounds, count; struct auth_user users[AUTH_USERS]; };
struct auth_io {
    /* load: 0 success, 1 absent, -1 damaged/unreadable. All writes must be durable. */
    int (*load)(struct auth_database *);
    int (*save)(const struct auth_database *);
    bool (*random)(uint8_t *, size_t);
    bool persistent;
};
void auth_init(const struct auth_io *);
void auth_tick(uint64_t seconds);
void auth_key(char);
bool auth_prompting(void);
bool auth_logged_in(void);
bool auth_admin(void);
uint32_t auth_uid(void);
const char *auth_name(void);
const char *auth_prompt(void);
const char *auth_status(void);
const char *auth_input(void); /* Masked for passwords. */
bool auth_command(const char *, void (*)(const char *));
bool auth_path(const char *, bool write);
void auth_lock(void);

struct sha256 { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; size_t used; };
struct password_kdf {
    struct sha256 inner, outer;
    uint8_t u[32], result[32];
    uint32_t done, rounds;
};
void sha256_digest(const void *, size_t, uint8_t [32]);
void password_begin(struct password_kdf *, const char *, const uint8_t [16], uint32_t);
bool password_step(struct password_kdf *, unsigned);
void auth_wipe(void *, size_t);
#endif
