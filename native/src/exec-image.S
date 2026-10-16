/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata
.global native_exec_start,native_exec_end
native_exec_start:
.incbin "build/user.elf"
native_exec_end:
.global native_hello_start,native_hello_end,native_spin_start,native_spin_end
native_hello_start:
.incbin "build/user/hello.elf"
native_hello_end:
native_spin_start:
.incbin "build/user/spin.elf"
native_spin_end:
.section .note.GNU-stack,"",@progbits
