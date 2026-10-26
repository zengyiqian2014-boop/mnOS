/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef _WIN32
.section .rdata,"dr"
#else
.section .rodata
#endif
.balign 16
.global linux_image_start,linux_image_end,linux_dtb_start,linux_dtb_end
linux_image_start:
#if MNOS_LINUX_GUEST
.incbin "build/guest/Image"
#endif
linux_image_end:
linux_dtb_start:
#if MNOS_LINUX_GUEST
.incbin "build/guest/machine.dtb"
#endif
linux_dtb_end:
#if defined(__x86_64__) && !defined(_WIN32)
.section .note.GNU-stack,"",@progbits
#endif
