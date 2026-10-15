/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_KERNEL_COMMON_H
#define MNOS_KERNEL_COMMON_H
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool identifier(const char *s, bool parameter) {
    if (!s || !*s || !isalnum((unsigned char)*s) || strlen(s) > 256) return false;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-' && !(parameter && *s == '.')) return false;
    return true;
}

static int execute(char *const argv[]) {
    /* Fixed programs and a clean environment; no shell or implicit elevation. */
    if (clearenv() || setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) || setenv("LANG", "C.UTF-8", 1)) return 1;
    execv(argv[0], argv); perror(argv[0]); return 1;
}
#endif
