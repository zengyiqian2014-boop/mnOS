/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit APT frontend. No shell, automatic elevation, unsigned repositories,
 * unattended -y, remote script execution or Electron sandbox overrides.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int run(char **args) {execv(args[0],args);perror(args[0]);return 1;}
static int usage(void) {
    puts("mnos-pkg list | check | search TEXT | show PACKAGE\n"
         "sudo mnos-pkg update | upgrade | install PACKAGE... | remove PACKAGE...\n"
         "Queries need no root. Changes require explicit sudo. APT asks for confirmation.\n"
         "Uses the configured signed Debian/Microsoft repositories. No --allow-unauthenticated.\n"
         "Details: help packages / help -cn packages");return 2;
}
static int package_name(const char *p) {
    if(!*p || !isalnum((unsigned char)*p))return 0;
    for(;*p;p++)if(!isalnum((unsigned char)*p) && *p!='+' && *p!='-' && *p!='.' && *p!=':')return 0;
    return 1;
}
int main(int argc,char **argv) {
    if(argc==1 || (argc==2 && !strcmp(argv[1],"--help"))){usage();return 0;}
    const char *cmd=argv[1];
    if(!strcmp(cmd,"list") && argc==2)return run((char *[]){"/usr/bin/dpkg-query","-W",NULL});
    if(!strcmp(cmd,"check") && argc==2)return run((char *[]){"/usr/bin/dpkg","--audit",NULL});
    if(!strcmp(cmd,"search") && argc==3 && *argv[2])return run((char *[]){"/usr/bin/apt-cache","search","--",argv[2],NULL});
    if(!strcmp(cmd,"show") && argc==3 && package_name(argv[2]))return run((char *[]){"/usr/bin/apt-cache","show","--",argv[2],NULL});
    int packages=!strcmp(cmd,"install") || !strcmp(cmd,"remove");
    int global=!strcmp(cmd,"update") || !strcmp(cmd,"upgrade");
    if((!packages && !global) || (packages && argc<3) || (global && argc!=2) || argc>128)return usage();
    for(int i=2;i<argc;i++)if(!package_name(argv[i])){fputs("Invalid package name. Options, paths and URLs are not package names.\n",stderr);return 2;}
    if(getuid()!=0 || geteuid()!=0){fputs("Permission denied: use sudo mnos-pkg for package changes.\n",stderr);return 1;}
    /* Package scripts run as root: use a deterministic system executable path. */
    if(setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1)){perror("PATH");return 1;}
    char *args[132]={"/usr/bin/apt-get",argv[1],"--"};
    for(int i=2;i<argc;i++) args[i+1]=argv[i];
    args[argc+1]=NULL;
    return run(args);
}
