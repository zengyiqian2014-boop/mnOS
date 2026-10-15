/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata.ap,"a"
.balign 16
.global ap_trampoline, ap_trampoline_end
.set AP_BASE, 0x7000
ap_trampoline:
.code16
 cli
 cld
 xor %ax, %ax
 mov %ax, %ds
 mov %ax, %es
 mov %ax, %ss
 lgdt AP_BASE + (ap_gdt_pointer - ap_trampoline)
 mov %cr0, %eax
 or $1, %eax
 mov %eax, %cr0
 ljmpl $8, $(AP_BASE + ap_protected - ap_trampoline)
.code32
ap_protected:
 mov $16, %ax
 mov %ax, %ds
 mov %ax, %es
 mov %ax, %ss
 mov %cr4, %eax
 or $(1 << 5), %eax
 mov %eax, %cr4
 mov 0x8000, %eax
 mov %eax, %cr3
 mov $0xc0000080, %ecx
 rdmsr
 or $((1 << 8) | (1 << 11)), %eax
 wrmsr
 mov %cr0, %eax
 or $((1 << 31) | (1 << 16)), %eax
 mov %eax, %cr0
 ljmpl $24, $(AP_BASE + ap_long - ap_trampoline)
.code64
ap_long:
 mov $16, %ax
 mov %ax, %ds
 mov %ax, %es
 mov %ax, %ss
 xor %ax, %ax
 mov %ax, %fs
 mov %ax, %gs
 mov 0x8008, %rsp
 xor %rbp, %rbp
 mov 0x8010, %rdi
 mov 0x8018, %rax
 call *%rax
1: cli
 hlt
 jmp 1b
.balign 8
ap_gdt:
 .quad 0, 0x00cf9b000000ffff, 0x00cf93000000ffff, 0x00af9b000000ffff
ap_gdt_pointer:
 .word 31
 .long AP_BASE + (ap_gdt - ap_trampoline)
ap_trampoline_end:
.section .note.GNU-stack,"",@progbits
