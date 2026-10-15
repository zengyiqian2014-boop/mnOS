/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        ++x;
        ++y;
    }
    return 0;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n--) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
bool streq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
void number(char *s, uint64_t n) {
    char tmp[21];
    unsigned i = 0, j = 0;
    do {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i)
        s[j++] = tmp[--i];
    s[j] = 0;
}
