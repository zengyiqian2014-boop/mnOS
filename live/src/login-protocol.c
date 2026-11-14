/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "login-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
int login_write(int fd,const void *data,size_t n) {
    const char *p=data;while(n) {ssize_t k=send(fd,p,n,MSG_NOSIGNAL);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;
}
int login_read(int fd,void *data,size_t n) {
    char *p=data;while(n) {ssize_t k=read(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;
}
int login_connect(void) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un addr={.sun_family=AF_UNIX};
    if(strlen(MNOS_LOGIN_SOCKET)>=sizeof addr.sun_path) {close(fd);return -1;}
    strcpy(addr.sun_path,MNOS_LOGIN_SOCKET);
    struct timeval timeout={180,0};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)||setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout)||connect(fd,(void *)&addr,sizeof addr)) {close(fd);return -1;}
    return fd;
}
int login_send(int fd,unsigned op,uint32_t uid,const char *password,const char *code) {
    size_t pn=strlen(password),cn=strlen(code);
    if(pn>1048576||cn>128)return -1;
    struct login_request q={LOGIN_MAGIC,op,uid,(uint32_t)pn,(uint32_t)cn};
    return login_write(fd,&q,sizeof q)||login_write(fd,password,pn)||login_write(fd,code,cn)?-1:0;
}
int login_configured(uint32_t uid) {
    char path[sizeof MNOS_LOGIN_STORE+32];snprintf(path,sizeof path,"%s/%u.factor",MNOS_LOGIN_STORE,uid);
    struct stat st;if(lstat(path,&st))return errno==ENOENT?0:-1;
    return S_ISREG(st.st_mode)&&st.st_nlink==1&&!(st.st_mode&077)?1:-1;
}
int login_child(uint32_t uid,int install,int *child_pid) {
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return -1;
    char id[32];snprintf(id,sizeof id,"%u",uid);
    pid_t child=fork();
    if(child==0) {
        if(dup2(pair[1],0)<0||dup2(pair[1],1)<0)_exit(127);
        close(pair[0]);close(pair[1]);
        /* A privileged PAM caller may authenticate an account other than its
         * original real UID (su, LightDM). Unprivileged clients cannot do so. */
        if(geteuid()==0&&setuid(0))_exit(127);
        execl("/usr/lib/mnos/login-helper","login-helper",install?"--install":"--user",id,(char *)0);
        _exit(127);
    }
    close(pair[1]);if(child<0) {close(pair[0]);return -1;}
    struct timeval timeout={180,0};
    if(setsockopt(pair[0],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)||setsockopt(pair[0],SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout)) {close(pair[0]);return -1;}
    *child_pid=child;return pair[0];
}
