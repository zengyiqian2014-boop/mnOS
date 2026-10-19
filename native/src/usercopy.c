/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "kernel.h"
#include "usercopy.h"
void *user_buffer(const struct task *t, uint64_t ptr, uint64_t length, bool writable) {
    if (!t || !length || length > PAGE) return NULL;
    const uint64_t va[] = { USER_BASE, USER_DATA,
#ifdef __aarch64__
        USER_STACK
#else
        USER_STACK - PAGE
#endif
    };
    const uint64_t physical[] = {t->code, t->data, t->stack};
    for (unsigned i = writable ? 1 : 0; i < ARRAY_SIZE(va); ++i)
        if (physical[i] && ptr >= va[i] && ptr - va[i] < PAGE &&
            length <= PAGE - (ptr - va[i]))
            return phys_pointer(physical[i] + (ptr - va[i]));
    return NULL;
}
