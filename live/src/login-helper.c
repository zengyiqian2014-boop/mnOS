/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small privileged broker. The GTK client never runs setuid. Only the real
 * caller's UID is accepted unless the calling process was already root. */
#define _GNU_SOURCE
#include "login-protocol.h"
#include "auth.h"
#include <security/pam_appl.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
struct credentials {const char *name,*password;};
static int conversation(int n,const struct pam_message **messages,struct pam_response **out,void *data) {
    const struct credentials *c=data;
    if(n<1||n>PAM_MAX_NUM_MSG)return PAM_CONV_ERR;
    struct pam_response *r=calloc((size_t)n,sizeof *r);if(!r)return PAM_BUF_ERR;
    for(int i=0;i<n;++i) {
        const char *text=NULL;
        switch(messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:text=c->password;break;
        case PAM_PROMPT_ECHO_ON:text=c->name;break;
        case PAM_TEXT_INFO:case PAM_ERROR_MSG:break;
        default:goto fail;
        }
        if(text&&!(r[i].resp=strdup(text)))goto fail;
    }
    *out=r;return PAM_SUCCESS;
fail:
    for(int i=0;i<n;++i)if(r[i].resp) {auth_wipe(r[i].resp,strlen(r[i].resp));free(r[i].resp);}
    free(r);return PAM_CONV_ERR;
}
static int password_valid(const char *name,const char *password) {
    struct credentials credentials={name,password};struct pam_conv conv={conversation,&credentials};pam_handle_t *pam=NULL;
    int result=pam_start("mnos-security-setup",name,&conv,&pam);
    if(result==PAM_SUCCESS)result=pam_authenticate(pam,PAM_DISALLOW_NULL_AUTHTOK);
    if(result==PAM_SUCCESS)result=pam_acct_mgmt(pam,0);
    if(pam)pam_end(pam,result);return result==PAM_SUCCESS;
}
static int live_session(void) {
    FILE *f=fopen("/proc/cmdline","re");if(!f)return 1;
    char word[256];int live=0;while(fscanf(f,"%255s",word)==1)if(!strcmp(word,"boot=live"))live=1;
    fclose(f);return live;
}
static int install_server(int *child_pid) {
    struct passwd *service=getpwnam("login-process");if(!service||service->pw_uid==0)return -1;
    uid_t uid=service->pw_uid;gid_t gid=service->pw_gid;
    if(mkdir(MNOS_LOGIN_STORE,0700)&&errno!=EEXIST)return -1;
    int directory=open(MNOS_LOGIN_STORE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0)return -1;
    int result=fchown(directory,uid,gid)||fchmod(directory,0700);close(directory);if(result)return -1;
    int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return -1;
    pid_t child=fork();
    if(child==0) {
        close(pair[0]);close(0);close(1);
        if(setgroups(0,NULL)||setgid(gid)||setuid(uid)||prctl(PR_SET_DUMPABLE,0))_exit(1);
        alarm(180);login_serve(pair[1]);close(pair[1]);_exit(0);
    }
    close(pair[1]);if(child<0) {close(pair[0]);return -1;}
    *child_pid=child;return pair[0];
}
int main(int argc,char **argv) {
    uid_t caller=getuid();struct rlimit no_core={0,0};
    if(geteuid()!=0||setrlimit(RLIMIT_CORE,&no_core)||prctl(PR_SET_DUMPABLE,0))return 1;
    if(argc!=3||(strcmp(argv[1],"--user")&&strcmp(argv[1],"--install")))return 1;
    int install=!strcmp(argv[1],"--install");char *end;errno=0;unsigned long id=strtoul(argv[2],&end,10);
    if(errno||!*argv[2]||*end||id>UINT32_MAX||(caller!=0&&id!=caller)||(install&&caller!=0))return 1;
    clearenv();if(setenv("PATH","/usr/sbin:/usr/bin:/sbin:/bin",1)||setenv("LC_ALL","C.UTF-8",1))return 1;
    signal(SIGPIPE,SIG_IGN);alarm(180);umask(077);
    struct login_request q={0};struct login_response response={.result=LOGIN_ERROR};
    char *password=NULL,code[129]={0};int server=-1,child=-1;
    if(login_read(0,&q,sizeof q)||q.magic!=LOGIN_MAGIC||q.uid!=id||q.password_bytes>1048576||q.code_bytes>128||q.operation<LOGIN_STATUS||q.operation>LOGIN_DISABLE)goto end;
    password=calloc(1,(size_t)q.password_bytes+1);if(!password||mlock(password,(size_t)q.password_bytes+1))goto end;
    if(login_read(0,password,q.password_bytes)||login_read(0,code,q.code_bytes)||memchr(password,0,q.password_bytes)||memchr(code,0,q.code_bytes))goto end;
    if(q.operation==LOGIN_STATUS) {
        int configured=login_configured(q.uid);response.result=configured==0?LOGIN_ABSENT:configured==1?LOGIN_OK:LOGIN_ERROR;goto end;
    }
    if((install&&q.operation!=LOGIN_ENROLL)||(!install&&live_session()&&q.operation!=LOGIN_VERIFY))goto end;
    struct passwd *user=getpwuid(q.uid);
    if(!user||!password_valid(user->pw_name,password)) {response.result=LOGIN_DENIED;goto end;}
    server=install?install_server(&child):login_connect();if(server<0)goto end;
    if(login_send(server,q.operation,q.uid,password,code))goto end;
    auth_wipe(password,q.password_bytes);auth_wipe(code,sizeof code);
    if(login_read(server,&response,sizeof response)) {response.result=LOGIN_ERROR;goto end;}
    if(response.result==LOGIN_SETUP&&q.operation==LOGIN_ENROLL) {
        if(login_write(1,&response,sizeof response))goto cleanup;
        auth_wipe(&response,sizeof response);response.result=LOGIN_ERROR;
        uint32_t length;if(login_read(0,&length,sizeof length)||length!=6||login_read(0,code,length))goto end;
        if(login_write(server,&length,sizeof length)||login_write(server,code,length)||login_read(server,&response,sizeof response))response.result=LOGIN_ERROR;
    }
end:
    auth_wipe(response.secret,sizeof response.secret);auth_wipe(response.recovery,sizeof response.recovery);
    (void)login_write(1,&response,sizeof response);
cleanup:
    if(password) {auth_wipe(password,(size_t)q.password_bytes+1);munlock(password,(size_t)q.password_bytes+1);free(password);}
    auth_wipe(code,sizeof code);auth_wipe(&response,sizeof response);
    if(server>=0)close(server);if(child>0)while(waitpid(child,NULL,0)<0&&errno==EINTR){}return 0;
}
