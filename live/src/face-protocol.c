/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "face-protocol.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int face_child(uint32_t uid,int pam,int *pid) {
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return -1;
    char id[32];snprintf(id,sizeof id,"%u",uid);pid_t child=fork();
    if(!child) {
        if(dup2(pair[1],0)<0||dup2(pair[1],1)<0)_exit(127);
        close(pair[0]);close(pair[1]);
        if(geteuid()==0&&setuid(0))_exit(127);
        execl(MNOS_FACE_HELPER,"face-helper",pam?"--pam":"--user",id,(char *)0);_exit(127);
    }
    close(pair[1]);if(child<0) {close(pair[0]);return -1;}
    struct timeval t={180,0};
    if(setsockopt(pair[0],SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t)||setsockopt(pair[0],SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t)) {close(pair[0]);return -1;}
    *pid=child;return pair[0];
}
int face_send(int fd,unsigned op,uint32_t uid,const char *password,const char *totp,const char *recovery,const char *display) {
    size_t pn=strlen(password),tn=strlen(totp),rn=strlen(recovery),dn=display?strlen(display):0;
    if(pn>1048576||tn>128||rn>32||dn>=32)return -1;
    struct face_request q={.magic=FACE_MAGIC,.operation=op,.uid=uid,.password_bytes=(uint32_t)pn,.totp_bytes=(uint32_t)tn,.recovery_bytes=(uint32_t)rn};
    if(display)memcpy(q.display,display,dn);
    return login_write(fd,&q,sizeof q)||login_write(fd,password,pn)||login_write(fd,totp,tn)||login_write(fd,recovery,rn)?-1:0;
}
