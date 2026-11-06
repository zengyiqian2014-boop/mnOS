/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_LINUX_GUEST_H
#define MNOS_LINUX_GUEST_H
#include <stdbool.h>
#include <stdint.h>
bool linux_guest_command(const char *, void (*)(const char *));
bool linux_guest_open(const char *app, void (*)(const char *));
bool linux_guest_enabled(void);
bool linux_guest_prompting(void);
const char *linux_guest_requested_app(void);
bool linux_guest_confirm(bool enable, void (*)(const char *));
void linux_guest_tick(void);
void linux_guest_key(unsigned char);
void linux_guest_clear(void);
const char *linux_guest_status(void);
const char *linux_guest_line(unsigned);
unsigned linux_guest_lines(void);
uint64_t linux_guest_instructions(void);
#endif
