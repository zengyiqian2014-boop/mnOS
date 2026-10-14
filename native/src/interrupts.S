/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .text
.code64
.macro ISR n
.global isr\n
isr\n:
 .if (\n != 8) && (\n != 10) && (\n != 11) && (\n != 12) && (\n != 13) && (\n != 14) && (\n != 17) && (\n != 21) && (\n != 29) && (\n != 30)
 pushq $0
 .endif
 pushq $\n
 jmp irq_common
.endm
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,128,255
 ISR \n
.endr
irq_common:
 cld
 push %rax
 push %rbx
 push %rcx
 push %rdx
 push %rsi
 push %rdi
 push %rbp
 push %r8
 push %r9
 push %r10
 push %r11
 push %r12
 push %r13
 push %r14
 push %r15
 mov %rsp, %rdi
 and $-16, %rsp
 call irq_dispatch
 jmp irq_return
.global enter_frame
enter_frame:
 /* C enters with RDI = frame. Interrupt return jumps below this move. */
 mov %rdi, %rsp
 jmp frame_return
.global irq_return
irq_return:
 mov %rax, %rsp
frame_return:
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %r11
 pop %r10
 pop %r9
 pop %r8
 pop %rbp
 pop %rdi
 pop %rsi
 pop %rdx
 pop %rcx
 pop %rbx
 pop %rax
 add $16, %rsp
 iretq
.section .rodata
.global isr_table
isr_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,128,255
 .quad isr\n
.endr
.section .note.GNU-stack,"",@progbits
