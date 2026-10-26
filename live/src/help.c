/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef MNOS_MANUAL_DIR
#define MNOS_MANUAL_DIR "/usr/share/doc/mnos"
#endif
static const char *topics[]={"help","tools","commands","ctf","firefox","code","packages","build","network","kernel","system","files","native","editions","management"};
static int known(const char *s){for(size_t i=0;i<sizeof topics/sizeof *topics;i++)if(!strcmp(s,topics[i]))return 1;return !strcmp(s,"all");}
static int has_manual(const char *topic) {
    pid_t child=fork();
    if(child<0)return 0;
    if(!child){
        int null=open("/dev/null",O_WRONLY);
        if(null<0 || dup2(null,STDOUT_FILENO)<0 || dup2(null,STDERR_FILENO)<0)_exit(1);
        if(null>STDERR_FILENO)close(null);
        execl("/usr/bin/man","man","--where","--",topic,(char *)NULL);
        _exit(1);
    }
    int status=0;pid_t waited;
    do{waited=waitpid(child,&status,0);}while(waited<0 && errno==EINTR);
    return waited==child && WIFEXITED(status) && WEXITSTATUS(status)==0;
}
int main(int argc,char **argv){
    int cn=0,pos=1;
    if(argc==3 && !strcmp(argv[1],"--has-topic"))return known(argv[2])?0:1;
    if(pos<argc && !strcmp(argv[pos],"-cn")){cn=1;pos++;}
    const char *topic=pos<argc?argv[pos++]:"all";
    if(pos!=argc){fputs("Usage: help [-cn] [TOPIC|all|TOOL]\n",stderr);return 2;}
    if(!strcmp(topic,"--help"))topic="help";
    if(!strcmp(topic,"--list")){for(size_t i=0;i<sizeof topics/sizeof *topics;i++)puts(topics[i]);return 0;}
    if(!strcmp(topic,"firefox-esr"))topic="firefox";
    if(!strcmp(topic,"mnos-help"))topic="help";
    if(!strcmp(topic,"mnos-admin"))topic="management";
    if(!known(topic)){
        if(!*topic || *topic=='-' || strchr(topic,'/'))return 2;
        if(cn)puts("下面是工具随包提供的手册或帮助；语言取决于上游。\n");
        fflush(stdout);
        if(has_manual(topic))execl("/usr/bin/man","man","--",topic,(char *)NULL);
        else execl("/usr/bin/mnos-center","mnos-center","--print-help",topic,(char *)NULL);
        perror("tool help");return 1;
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
