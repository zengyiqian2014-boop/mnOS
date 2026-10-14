/* SPDX-License-Identifier: GPL-3.0-or-later
 * Position-independent ring-3 test programs. No Linux ABI or kernel code.
 */
.section .rodata
.global user_loop, user_loop_end, user_read_kernel, user_read_kernel_end
.global user_nx, user_nx_end, user_guard, user_guard_end, user_pointer, user_pointer_end
.global user_write_code, user_write_code_end, user_port_io, user_port_io_end
user_loop:
 movabs $0x100001000, %rbx
1: incq (%rbx)
 cmpq $0, 8(%rbx)
 je 3f
 mov $5, %eax
 mov $10, %edi
 int $0x80
3:
 pause
 jmp 1b
user_loop_end:
user_read_kernel:
 movabs $0x100000, %rdi
 mov (%rdi), %rax
 ud2
user_read_kernel_end:
user_nx:
 movabs $0x100001000, %rax
 jmp *%rax
user_nx_end:
user_guard:
 movabs $0x100002000, %rax
 movq $1, (%rax)
 ud2
user_guard_end:
user_pointer:
 xor %eax, %eax
 movabs $0x100000, %rdi
 mov $16, %esi
 int $0x80
 cmp $-14, %rax
 jne 2f
 /* Unmapped guard page, length overflow, and a cross-page read. */
 xor %eax, %eax
 movabs $0x100002000, %rdi
 mov $1, %esi
 int $0x80
 cmp $-14, %rax
 jne 2f
 xor %eax, %eax
 movabs $0x100001000, %rdi
 mov $257, %esi
 int $0x80
 cmp $-14, %rax
 jne 2f
 xor %eax, %eax
 movabs $0x100001fff, %rdi
 mov $2, %esi
 int $0x80
 cmp $-14, %rax
 jne 2f
 mov $4, %eax
 int $0x80
2: mov $3, %eax
 int $0x80
 ud2
user_pointer_end:
user_write_code:
 movabs $0x100000000, %rdi
 movb $0, (%rdi)
 ud2
user_write_code_end:
user_port_io:
 mov $0x80, %dx
 xor %eax, %eax
 out %al, %dx
 ud2
user_port_io_end:
.section .note.GNU-stack,"",@progbits
