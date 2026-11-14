/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runs as root ONLY in the disposable authentication test container. */
#define _GNU_SOURCE
#include "login-protocol.h"
#include "auth.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <openssl/hmac.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static const char *password="Test-only-password-491";
static pid_t daemon_pid=-1;
static void daemon_start(void) {
    struct passwd *p=getpwnam("login-process");assert(p);uid_t uid=p->pw_uid;gid_t gid=p->pw_gid;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);assert(fd>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,MNOS_LOGIN_SOCKET);unlink(MNOS_LOGIN_SOCKET);
    assert(!bind(fd,(void *)&address,sizeof address)&&!chmod(MNOS_LOGIN_SOCKET,0600)&&!listen(fd,16));
    daemon_pid=fork();assert(daemon_pid>=0);
    if(!daemon_pid) {
        assert(dup2(fd,3)==3);if(fd!=3)close(fd);assert(!fcntl(3,F_SETFD,0));
        assert(!setgroups(0,NULL)&&!setgid(gid)&&!setuid(uid));
        char pid[32];snprintf(pid,sizeof pid,"%ld",(long)getpid());assert(!setenv("LISTEN_PID",pid,1)&&!setenv("LISTEN_FDS","1",1));
        execl("/usr/lib/mnos/login-process","login-process",(char *)0);_exit(127);
    }
    close(fd);
}
static void daemon_stop(void) {if(daemon_pid>0) {kill(daemon_pid,SIGTERM);waitpid(daemon_pid,NULL,0);daemon_pid=-1;}unlink(MNOS_LOGIN_SOCKET);}
static int request(unsigned operation,uid_t uid,const char *pw,const char *code) {
    int child=-1,fd=login_child(uid,0,&child);assert(fd>=0);
    assert(!login_send(fd,operation,uid,pw,code));struct login_response r;
    int result=login_read(fd,&r,sizeof r)?LOGIN_ERROR:(int)r.result;close(fd);waitpid(child,NULL,0);return result;
}
static void oracle(const uint8_t key[32],char text[7]) {
    uint64_t step=(uint64_t)time(NULL)/30;uint8_t counter[8],digest[EVP_MAX_MD_SIZE];unsigned length;
    for(unsigned i=0;i<8;++i)counter[7-i]=(uint8_t)(step>>(8*i));
    assert(HMAC(EVP_sha1(),key,32,counter,8,digest,&length)&&length==20);
    unsigned offset=digest[19]&15;uint32_t n=((uint32_t)(digest[offset]&127)<<24)|((uint32_t)digest[offset+1]<<16)|((uint32_t)digest[offset+2]<<8)|digest[offset+3];
    snprintf(text,7,"%06u",n%1000000);OPENSSL_cleanse(digest,sizeof digest);
}
struct answers {const char *pw,*code;unsigned factor_prompts;};
static int converse(int n,const struct pam_message **messages,struct pam_response **out,void *data) {
    struct answers *a=data;struct pam_response *response=calloc((size_t)n,sizeof *response);assert(response);
    for(int i=0;i<n;++i)if(messages[i]->msg_style==PAM_PROMPT_ECHO_OFF) {
        int factor=strstr(messages[i]->msg,"Authenticator")!=NULL;if(factor)++a->factor_prompts;
        response[i].resp=strdup(factor?a->code:a->pw);assert(response[i].resp);
    }
    *out=response;return PAM_SUCCESS;
}
static int pam_login(const char *user,const char *pw,const char *code,unsigned *prompts) {
    struct answers a={pw,code,0};struct pam_conv conversation={converse,&a};pam_handle_t *pam=NULL;
    int result=pam_start("mnos-auth-test",user,&conversation,&pam);assert(result==PAM_SUCCESS);
    result=pam_authenticate(pam,0);pam_end(pam,result);if(prompts)*prompts=a.factor_prompts;return result;
}
static void wait_retry(void) {sleep(3);}
int main(void) {
    assert(geteuid()==0&&getenv("MNOS_AUTH_TEST_CONTAINER"));atexit(daemon_stop);daemon_start();
    assert(request(LOGIN_STATUS,1000,"","")==LOGIN_ABSENT);
    assert(request(LOGIN_ENROLL,1000,"wrong-password","")==LOGIN_DENIED);
    int child=-1,fd=login_child(1000,0,&child);assert(fd>=0&&!login_send(fd,LOGIN_ENROLL,1000,password,""));
    struct login_response setup;assert(!login_read(fd,&setup,sizeof setup)&&setup.result==LOGIN_SETUP);
    char code[7];oracle(setup.secret,code);uint32_t size=6;assert(!login_write(fd,&size,4)&&!login_write(fd,code,6));
    struct login_response result;assert(!login_read(fd,&result,sizeof result)&&result.result==LOGIN_OK);close(fd);waitpid(child,NULL,0);
    assert(request(LOGIN_STATUS,1000,"","")==LOGIN_OK);
    unsigned prompts=0;assert(pam_login("demo","wrong-password",code,&prompts)!=PAM_SUCCESS&&prompts==0);
    assert(pam_login("demo",password,code,&prompts)!=PAM_SUCCESS&&prompts==1);wait_retry(); /* enrollment replay */
    assert(pam_login("demo",password,setup.recovery[0],NULL)==PAM_SUCCESS);
    assert(pam_login("demo",password,setup.recovery[0],NULL)!=PAM_SUCCESS);wait_retry();
    fd=open(MNOS_LOGIN_STORE "/1000.factor",O_RDONLY);assert(fd>=0);uint8_t stored[4096];ssize_t bytes=read(fd,stored,sizeof stored);close(fd);
    assert(bytes>0&&!memmem(stored,(size_t)bytes,setup.secret,32));
    assert(!chmod(MNOS_LOGIN_STORE,0500));assert(pam_login("demo",password,setup.recovery[1],NULL)!=PAM_SUCCESS);
    assert(!chmod(MNOS_LOGIN_STORE,0700));assert(pam_login("demo",password,setup.recovery[1],NULL)==PAM_SUCCESS);
    struct answers a={password,"",0};struct pam_conv conversation={converse,&a};pam_handle_t *pam=NULL;
    assert(pam_start("mnos-auth-test","demo",&conversation,&pam)==PAM_SUCCESS);int changed=pam_chauthtok(pam,0);pam_end(pam,changed);assert(changed!=PAM_SUCCESS);
    daemon_stop();assert(pam_login("demo",password,setup.recovery[2],NULL)!=PAM_SUCCESS);
    assert(pam_login("other",password,"",NULL)==PAM_SUCCESS);daemon_start();
    pid_t ordinary=fork();assert(ordinary>=0);
    if(!ordinary) {
        assert(!setgroups(0,NULL)&&!setgid(1001)&&!setuid(1001));
        assert(open(MNOS_LOGIN_STORE "/1000.factor",O_RDONLY)<0);
        int pid=-1,s=login_child(1000,0,&pid);assert(s>=0);(void)login_send(s,LOGIN_STATUS,1000,"","");
        struct login_response r;assert(login_read(s,&r,sizeof r)<0);close(s);waitpid(pid,NULL,0);_exit(0);
    }
    int status;assert(waitpid(ordinary,&status,0)==ordinary&&WIFEXITED(status)&&!WEXITSTATUS(status));
    /* Fresh independent TOTP through the actual PAM stack, after the enrollment
     * interval. The wait is bounded by one 30-second period. */
    uint64_t step=(uint64_t)time(NULL)/30;while((uint64_t)time(NULL)/30==step)usleep(100000);
    oracle(setup.secret,code);assert(pam_login("demo",password,code,NULL)==PAM_SUCCESS);
    assert(request(LOGIN_DISABLE,1000,password,setup.recovery[2])==LOGIN_OK);
    assert(request(LOGIN_STATUS,1000,"","")==LOGIN_ABSENT&&pam_login("demo",password,"",NULL)==PAM_SUCCESS);
    /* The guard is an Additional PAM module: it checks the preliminary phase
     * without stealing pam_unix's responsibility for collecting a new token. */
    a.pw="Replaced-test-password-812";pam=NULL;
    assert(pam_start("mnos-auth-test","demo",&conversation,&pam)==PAM_SUCCESS);
    changed=pam_chauthtok(pam,0);pam_end(pam,changed);assert(changed==PAM_SUCCESS);
    assert(pam_login("demo",a.pw,"",NULL)==PAM_SUCCESS&&pam_login("demo",password,"",NULL)!=PAM_SUCCESS);
    /* Restore the known disposable fixture for the graphical enrollment test. */
    a.pw=password;pam=NULL;assert(pam_start("mnos-auth-test","demo",&conversation,&pam)==PAM_SUCCESS);
    changed=pam_chauthtok(pam,0);pam_end(pam,changed);assert(changed==PAM_SUCCESS);
    /* Cancelled setup never creates a record. */
    fd=login_child(1001,1,&child);assert(fd>=0&&!login_send(fd,LOGIN_ENROLL,1001,password,""));
    assert(!login_read(fd,&result,sizeof result)&&result.result==LOGIN_SETUP);close(fd);waitpid(child,NULL,0);
    assert(request(LOGIN_STATUS,1001,"","")==LOGIN_ABSENT);
    OPENSSL_cleanse(&setup,sizeof setup);OPENSSL_cleanse(&result,sizeof result);OPENSSL_cleanse(stored,sizeof stored);
    puts("PASS Linux: actual daemon UID/socket, PAM password-first login, independent TOTP, replay/recovery, encrypted storage, cross-user denial, password-change guard, unavailable service, failed persistence, disable and cancelled installer setup");
    return 0;
}
