/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.global _start
_start:
 xor %eax,%eax
 mov %ax,%gs
 movabs $0x100001000,%rbx
1: incq (%rbx)
 cmpq $0,8(%rbx)
 je 2f
 mov $5,%eax
 mov $10,%edi
 int $0x80
2: pause
 jmp 1b
