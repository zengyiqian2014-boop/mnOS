/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "commands.h"
#include <stdio.h>
#include <string.h>
static void emit(const char *s){puts(s);}
int main(int argc,char **argv){
    if(argc!=2 || (strcmp(argv[1],"en") && strcmp(argv[1],"zh")))return 2;
    command_help(!strcmp(argv[1],"zh")?"-cn all":"all",emit);
}
