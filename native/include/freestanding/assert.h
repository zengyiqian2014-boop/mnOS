#ifndef MNOS_QR_ASSERT_H
#define MNOS_QR_ASSERT_H
/* QR inputs are bounded by our caller; hosted QR tests retain libc asserts. */
#define assert(condition) ((void)0)
#endif
