/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_LOCALE_H
#define MNOS_LOCALE_H
#include <stdlib.h>
#include <string.h>
/* Follow the process locale; an explicit help -cn/-en overrides this. */
static inline int mnos_chinese(void) {
    const char *value = getenv("LC_ALL");
    if (!value || !*value) value = getenv("LC_MESSAGES");
    if (!value || !*value) value = getenv("LANG");
    return value && !strncmp(value, "zh", 2) &&
        (!value[2] || value[2] == '_' || value[2] == '-' || value[2] == '.');
}
static inline const char *mnos_text(const char *english, const char *chinese) {
    return mnos_chinese() ? chinese : english;
}
#endif
