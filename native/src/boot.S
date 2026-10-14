/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .multiboot,"a"
.align 8
mb_header:
 .long 0xe85250d6
 .long 0
 .long mb_end - mb_header
 .long -(0xe85250d6 + (mb_end - mb_header))
 .short 5, 1
 .long 20
 .long 1280, 720, 32
 .align 8
 .short 0, 0
 .long 8
mb_end:

.section .text.boot,"ax"
.code32
.global _start
_start:
 cli
 cld
 mov $boot_stack_top, %esp
 cmp $0x36d76289, %eax
 jne boot_fail
 mov %ebx, saved_mb
 mov $0x80000001, %eax
 cpuid
 test $(1 << 29), %edx
 jz boot_fail
 test $(1 << 20), %edx
 jz boot_fail
 mov $early_pdpt, %eax
 or $3, %eax
 mov %eax, early_pml4
 mov $early_pd, %eax
 mov $early_pdpt, %edi
 mov $4, %ecx
1:
 mov %eax, %edx
 or $3, %edx
 mov %edx, (%edi)
 add $4096, %eax
 add $8, %edi
 loop 1b
 mov $early_pd, %edi
 xor %ecx, %ecx
2:
 mov %ecx, %eax
 shl $21, %eax
 or $0x83, %eax
 mov %eax, (%edi)
 add $8, %edi
 inc %ecx
 cmp $2048, %ecx
 jne 2b
 mov $early_pml4, %eax
 mov %eax, %cr3
 mov %cr4, %eax
 or $(1 << 5), %eax
 mov %eax, %cr4
 mov $0xc0000080, %ecx
 rdmsr
 or $((1 << 8) | (1 << 11)), %eax
 wrmsr
 lgdt gdt_pointer
 mov %cr0, %eax
 or $((1 << 31) | (1 << 16)), %eax
 mov %eax, %cr0
 ljmp $8, $long_entry
boot_fail:
 mov $0x3f8, %dx
 mov $'E', %al
 out %al, %dx
3: hlt
 jmp 3b
.code64
long_entry:
 mov $16, %ax
 mov %ax, %ds
 mov %ax, %es
 mov %ax, %ss
 xor %ax, %ax
 mov %ax, %fs
 mov %ax, %gs
 mov $boot_stack_top, %rsp
 xor %rbp, %rbp
 mov saved_mb(%rip), %edi
 call kernel_main
4: cli
 hlt
 jmp 4b

.section .data
.align 16
.global gdt64
gdt64:
 .quad 0
 .quad 0x00af9a000000ffff
 .quad 0x00cf92000000ffff
 .quad 0x00affa000000ffff
 .quad 0x00cff2000000ffff
 .quad 0, 0
gdt_pointer:
 .word . - gdt64 - 1
 .quad gdt64
saved_mb: .long 0
.section .bss
.align 4096
early_pml4: .skip 4096
early_pdpt: .skip 4096
early_pd: .skip 16384
.global boot_stack_guard
boot_stack_guard: .skip 4096
boot_stack: .skip 65536
boot_stack_top:
.section .note.GNU-stack,"",@progbits
