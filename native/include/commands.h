/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_COMMANDS_H
#define MNOS_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
typedef void (*command_output)(const char *);
/* args: [-cn] [COMMAND|all]; kernel prefix is accepted for help topics. */
void command_help(const char *args, command_output output);
/* Expands one shared command prefix. Ambiguous choices are printed. */
bool command_complete(char *line, size_t capacity, command_output output);
unsigned command_count(void);
const char *command_name(unsigned index);
#endif
