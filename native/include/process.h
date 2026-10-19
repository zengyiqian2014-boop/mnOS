/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_PROCESS_H
#define MNOS_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Dynamic applications on the boot CPU; callers mask IRQs on that CPU.
 * Probe workers remain independent. No application migration yet. */
#define EXEC_FILE_LIMIT (64u * 1024u)
int process_start(const void *, size_t, const char *);
bool process_command(const char *, void (*)(const char *));
bool process_active(void);
bool process_dying(void);
bool process_sleeping(void);
struct task;
struct task *process_task(void);
struct task *process_choose(void);
void process_exit(int status, const char *reason);
void process_reap(void);
void process_session_check(void);
int64_t process_write(uint64_t pointer, uint64_t length);
int64_t process_syscall(uint64_t call, uint64_t arg, uint64_t length, bool *reschedule);
void process_tick(bool user);
#endif
