/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata
.global native_exec_start,native_exec_end
native_exec_start:
.incbin "build/user.elf"
native_exec_end:
.section .note.GNU-stack,"",@progbits
