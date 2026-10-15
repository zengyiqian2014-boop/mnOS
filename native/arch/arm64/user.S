/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.balign 16
.global arm_user_start,arm_user_end
arm_user_start:
 /* x0: probe number, x1: private data VA, x2: kernel data VA. */
 cmp x0,#2
 b.lo worker
 cmp x0,#2
 b.eq kernel_read
 cmp x0,#3
 b.eq data_exec
 cmp x0,#4
 b.eq stack_guard
 cmp x0,#5
 b.eq bad_pointer
 cmp x0,#6
 b.eq code_write
.global arm_user_privileged
arm_user_privileged:
 mrs x3,sctlr_el1
 b failed
kernel_read: ldr x3,[x2]
 b failed
data_exec: br x1
stack_guard: sub x3,sp,#4096
 ldr x3,[x3,#-8]
 b failed
bad_pointer:
 mov x0,x2
 mov x8,#0
 svc #0
 cmn x0,#1
 b.ne failed
 mov x8,#4
 svc #0
 b failed
code_write: adr x3,arm_user_start
 str xzr,[x3]
 b failed
worker:
 ldr x3,[x1]
 add x3,x3,#1
 str x3,[x1]
 /* Syscall-free spin gives timer preemption a real workload. */
 movz x4,#256,lsl #16
1: subs x4,x4,#1
 b.ne 1b
 mov x8,#5
 mov x0,#2
 svc #0
 b worker
failed:
 mov x8,#255
 svc #0
 b failed
arm_user_end:
