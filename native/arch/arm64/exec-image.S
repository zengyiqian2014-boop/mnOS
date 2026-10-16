/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef _WIN32
.section .rdata,"dr"
#else
.section .rodata
#endif
.global native_exec_start,native_exec_end
native_exec_start:
.incbin "build/arm64/user.elf"
native_exec_end:
