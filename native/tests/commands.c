/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "commands.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char out[100000];
static void emit(const char *s) {assert(strlen(out)+strlen(s)+2<sizeof out);strcat(out,s);strcat(out,"\n");}
int main(void) {
    assert(command_count()>=38);
    for(unsigned i=0;i<command_count();i++){
        out[0]=0;command_help(command_name(i),emit);
        assert(strstr(out,"Example:") && !strstr(out,"Unknown help topic"));
        char topic[100]="-cn ";strcat(topic,command_name(i));
        out[0]=0;command_help(topic,emit);assert(strstr(out,"示例："));
    }
    out[0]=0;command_help("-cn kernel display",emit);assert(strstr(out,"分辨率"));
    out[0]=0;command_help("all",emit);assert(strstr(out,"static mnOS ELF64") && strstr(out,"snapshot"));
    out[0]=0;command_help("missing",emit);assert(strstr(out,"Unknown help topic"));
    char line[256]="ker";assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"kernel "));
    strcpy(line,"kernel sec");assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"kernel security "));
    strcpy(line,"kernel disk un");assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"kernel disk unlock "));
    strcpy(line,"linux en");assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"linux enable "));
    strcpy(line,"kernel linux open sys");assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"kernel linux open system "));
    out[0]=0;command_help("linux",emit);assert(strstr(out,"Off by default")&&strstr(out,"Esc cancels"));
    out[0]=0;command_help("-cn linux",emit);assert(strstr(out,"默认关闭")&&strstr(out,"Esc 取消"));
    strcpy(line,"help -cn kernel mem");assert(command_complete(line,sizeof line,emit));assert(!strcmp(line,"help -cn kernel mem"));
    out[0]=0;strcpy(line,"kernel dis");assert(command_complete(line,sizeof line,emit));assert(strstr(out,"display") && strstr(out,"disk"));
    strcpy(line,"write /disk/home/demo/k");assert(!command_complete(line,sizeof line,emit));
    char small[4]="ker";assert(command_complete(small,sizeof small,emit));assert(!strcmp(small,"ker"));
    puts("COMMANDS PASS: English/Chinese manuals, namespace, shared-prefix and bounded completion");
}
