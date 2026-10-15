/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
.section .boot,"ax"
.global _start
_start:
 b primary
 .long 0
 .quad 0x80000
 .quad image_size
 .quad 0
 .quad 0,0,0
 .long 0x644d5241
 .long 0
primary:
 mov x20,x0
 bl to_el1
 adrp x0,bss_start
 add x0,x0,:lo12:bss_start
 adrp x1,bss_end
 add x1,x1,:lo12:bss_end
1: cmp x0,x1
 b.hs 2f
 stp xzr,xzr,[x0],#16
 b 1b
2: adrp x0,boot_stack
 add x0,x0,:lo12:boot_stack
 add x0,x0,#16,lsl #12
 mov sp,x0
 mov x0,x20
 bl arm_start
#endif
.text
/* UEFI enters at EL1. Direct virt boot may enter at EL2. */
to_el1:
 msr daifset,#15
 mrs x1,CurrentEL
 cmp x1,#8
 b.ne 1f
 mov x1,#(1 << 31)
 msr hcr_el2,x1
 mov x1,#3
 msr cnthctl_el2,x1
 msr cntvoff_el2,xzr
 msr cptr_el2,xzr
 mov x1,#0x3c5
 msr spsr_el2,x1
 adr x1,1f
 msr elr_el2,x1
 eret
1: msr spsel,#1
 ret
.global arm_secondary
arm_secondary:
 mov x20,x0
 bl to_el1
 /* Context is an allocated CPU; stack top passed in first field temporarily. */
 ldr x1,[x20]
 mov sp,x1
 mov x0,x20
 bl arm_ap_main
3: wfi
11: wfi
 b 11b
.global arm_enter
arm_enter:
 mov sp,x0
 b restore
.balign 2048
.global arm_vectors
arm_vectors:
.macro vector kind
 .balign 128
 stp x0,x1,[sp,#-16]!
 mov x0,#\kind
 b save
.endm
 vector 0
 vector 1
 vector 2
 vector 3
 vector 4
 vector 5
 vector 6
 vector 7
 vector 8
 vector 9
 vector 10
 vector 11
 vector 12
 vector 13
 vector 14
 vector 15
save:
 sub sp,sp,#272
 stp x2,x3,[sp,#16]
 stp x4,x5,[sp,#32]
 stp x6,x7,[sp,#48]
 stp x8,x9,[sp,#64]
 stp x10,x11,[sp,#80]
 stp x12,x13,[sp,#96]
 stp x14,x15,[sp,#112]
 stp x16,x17,[sp,#128]
 stp x18,x19,[sp,#144]
 stp x20,x21,[sp,#160]
 stp x22,x23,[sp,#176]
 stp x24,x25,[sp,#192]
 stp x26,x27,[sp,#208]
 stp x28,x29,[sp,#224]
 str x30,[sp,#240]
 mrs x2,sp_el0
 mrs x3,elr_el1
 mrs x4,spsr_el1
 stp x2,x3,[sp,#248]
 str x4,[sp,#264]
 mov x1,x0
 ldp x2,x3,[sp,#272]
 stp x2,x3,[sp]
 mov x0,sp
 bl arm_exception
 mov sp,x0
restore:
 ldp x2,x3,[sp,#248]
 ldr x4,[sp,#264]
 msr sp_el0,x2
 msr elr_el1,x3
 msr spsr_el1,x4
 ldp x0,x1,[sp]
 ldp x2,x3,[sp,#16]
 ldp x4,x5,[sp,#32]
 ldp x6,x7,[sp,#48]
 ldp x8,x9,[sp,#64]
 ldp x10,x11,[sp,#80]
 ldp x12,x13,[sp,#96]
 ldp x14,x15,[sp,#112]
 ldp x16,x17,[sp,#128]
 ldp x18,x19,[sp,#144]
 ldp x20,x21,[sp,#160]
 ldp x22,x23,[sp,#176]
 ldp x24,x25,[sp,#192]
 ldp x26,x27,[sp,#208]
 ldp x28,x29,[sp,#224]
 ldr x30,[sp,#240]
 add sp,sp,#288
 eret
.bss
.balign 16
boot_stack:
 .skip 65536
.text
.global arm_firmware_entry
arm_firmware_entry:
 msr daifset,#15
 /* UEFI uses identity addressing. Clean every data/unified cache by set/way
    before dropping the firmware translation/cache state. */
 mrs x0,clidr_el1
 ubfx x1,x0,#24,#3
 mov x2,#0
4: cmp x2,x1
 b.hs 8f
 add x3,x2,x2,lsl #1
 lsr x3,x0,x3
 and x3,x3,#7
 cmp x3,#2
 b.lo 7f
 lsl x3,x2,#1
 msr csselr_el1,x3
 isb
 mrs x4,ccsidr_el1
 and x5,x4,#7
 add x5,x5,#4
 mrs x12,id_aa64mmfr2_el1
 ubfx x12,x12,#20,#4
 cbnz x12,12f
 ubfx x6,x4,#3,#10
 ubfx x8,x4,#13,#15
 b 13f
12: ubfx x6,x4,#3,#21
 ubfx x8,x4,#32,#24
13: clz w7,w6
5: mov x9,x6
6: lsl x10,x9,x7
 lsl x11,x8,x5
 orr x10,x10,x11
 orr x10,x10,x3
 dc cisw,x10
 subs x9,x9,#1
 b.ge 6b
 subs x8,x8,#1
 b.ge 5b
7: add x2,x2,#1
 b 4b
8: dsb sy
 mrs x0,CurrentEL
 cmp x0,#8
 b.ne 9f
 mrs x0,sctlr_el2
 bic x0,x0,#1
 bic x0,x0,#4
 bic x0,x0,#4096
 msr sctlr_el2,x0
 isb
 mov x0,sp
 msr sp_el1,x0
 bl to_el1
 b 10f
9: mrs x0,sctlr_el1
 bic x0,x0,#1
 bic x0,x0,#4
 bic x0,x0,#4096
 msr sctlr_el1,x0
 isb
10: ic iallu
 dsb sy
 isb
 bl arm_main
11: wfi
 b 11b
