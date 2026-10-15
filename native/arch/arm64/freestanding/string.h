/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_FREESTANDING_STRING_H
#define MNOS_FREESTANDING_STRING_H
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
size_t strlen(const char *);
#endif
