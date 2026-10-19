/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef MNOS_MANUAL_DIR
#define MNOS_MANUAL_DIR "/usr/share/doc/mnos"
#endif
static const char *topics[]={"help","tools","firefox","code","packages","build","network","kernel","system","files","native"};
static int known(const char *s){for(size_t i=0;i<sizeof topics/sizeof *topics;i++)if(!strcmp(s,topics[i]))return 1;return !strcmp(s,"all");}
int main(int argc,char **argv){
    int cn=0,pos=1;
    if(argc==3 && !strcmp(argv[1],"--has-topic"))return known(argv[2])?0:1;
    if(pos<argc && !strcmp(argv[pos],"-cn")){cn=1;pos++;}
    const char *topic=pos<argc?argv[pos++]:"all";
    if(pos!=argc){fputs("Usage: help [-cn] [TOPIC|all|TOOL]\n",stderr);return 2;}
    if(!strcmp(topic,"--help"))topic="help";
    if(!strcmp(topic,"--list")){for(size_t i=0;i<sizeof topics/sizeof *topics;i++)puts(topics[i]);return 0;}
    if(!strcmp(topic,"firefox-esr"))topic="firefox";
    if(!known(topic)){
        if(!*topic || *topic=='-' || strchr(topic,'/'))return 2;
        if(cn)puts("该工具的详细参数使用其随包安装的手册；手册语言取决于上游。\n");
        fflush(stdout);execl("/usr/bin/man","man","--",topic,(char *)NULL);perror("man");return 1;
    }
    char path[256];snprintf(path,sizeof path,MNOS_MANUAL_DIR "/commands.%s.txt",cn?"zh":"en");
    FILE *f=fopen(path,"r");if(!f){perror(path);return 1;}
    char *line=NULL;size_t capacity=0;int show=0,found=0,all=!strcmp(topic,"all");
    while(getline(&line,&capacity,f)>=0){
        if(!strncmp(line,"@@",2)){
            line[strcspn(line,"\r\n")]=0;show=all || !strcmp(line+2,topic);found|=show;
            continue;
        }
        if(show)fputs(line,stdout);
    }
    int error=ferror(f);free(line);fclose(f);return error || !found;
}
