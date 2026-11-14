/* SPDX-License-Identifier: GPL-3.0-or-later
 * No login shell, no credential logging, no plaintext credential files.
 * Privileged PAM/helper clients are authenticated with SO_PEERCRED. Root is
 * NOT a hardware isolation boundary; see AUTHENTICATION.md for the threat model.
 */
#define _GNU_SOURCE
#include "login-protocol.h"
#include "auth.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
struct stored_factor {uint32_t magic,version,failures,reserved;uint64_t retry_after;struct factor_record factor;};
static bool random_bytes(uint8_t *p,size_t n) {while(n) {ssize_t k=getrandom(p,n,0);if(k<0&&errno==EINTR)continue;if(k<=0)return false;p+=k;n-=(size_t)k;}return true;}
static int read_all(int fd,void *p,size_t n) {return login_read(fd,p,n);}
static int write_all(int fd,const void *data,size_t n) {const uint8_t *p=data;while(n) {ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;}
static int open_store(void) {
    int fd=open(MNOS_LOGIN_STORE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0)return -1;
    if(fstat(fd,&st)||st.st_uid!=geteuid()||(st.st_mode&077)) {close(fd);return -1;}return fd;
}
static int load_record(int dir,const char *name,struct stored_factor *r) {
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno==ENOENT?1:-1;
    struct stat st;int result=fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size!=sizeof *r;
    if(!result)result=read_all(fd,r,sizeof *r);close(fd);
    if(result||r->magic!=LOGIN_MAGIC||r->version!=1||r->factor.enabled!=1||r->failures>6)return -1;return 0;
}
static int store_record(int dir,const char *name,const struct stored_factor *r) {
    char temporary[80];uint8_t nonce[8];if(!random_bytes(nonce,sizeof nonce))return -1;
    char suffix[17];for(unsigned i=0;i<8;++i)snprintf(suffix+i*2,3,"%02x",nonce[i]);
    snprintf(temporary,sizeof temporary,".%s.%s",name,suffix);
    int fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;
    int result=write_all(fd,r,sizeof *r)||fsync(fd);if(close(fd))result=-1;
    if(!result)result=renameat(dir,temporary,dir,name)||fsync(dir);
    if(result)unlinkat(dir,temporary,0);return result?-1:0;
}
static void derive(const char *password,const uint8_t salt[16],uint8_t key[32]) {
    struct password_kdf work;password_begin(&work,password,salt,AUTH_ROUNDS);while(!password_step(&work,4096)){}memcpy(key,work.result,32);auth_wipe(&work,sizeof work);
}
static bool second_factor(struct factor_record *record,const uint8_t secret[32],const char *code,uint64_t now) {
    uint64_t step=0;if(factor_check(secret,now,record->last_step,code,&step)) {record->last_step=step;return true;}
    if(strlen(code)!=32)return false;
    uint8_t hash[32],zero[32]={0};bool found=false;factor_recovery_hash(code,record->salt,hash);
    for(unsigned i=0;i<FACTOR_RECOVERY;++i)if(!factor_same(record->recovery[i],zero,32)&&factor_same(hash,record->recovery[i],32)) {memset(record->recovery[i],0,32);found=true;}
    auth_wipe(hash,sizeof hash);return found;
}
void login_serve(int socket) {
    struct ucred peer;socklen_t size=sizeof peer;
    struct login_request request={0};struct login_response response={.result=LOGIN_ERROR};
    struct stored_factor record={0};uint8_t key[32]={0},secret[32]={0};
    char *password=NULL,code[129]={0},name[40],lock_name[40];int dir=-1,lock=-1;
    if(getsockopt(socket,SOL_SOCKET,SO_PEERCRED,&peer,&size)||peer.uid!=0)goto end;
    if(read_all(socket,&request,sizeof request)||request.magic!=LOGIN_MAGIC||request.operation<LOGIN_STATUS||request.operation>LOGIN_DISABLE||request.password_bytes>1048576||request.code_bytes>128)goto end;
    password=calloc(1,(size_t)request.password_bytes+1);if(!password)goto end;
    if(mlock(password,(size_t)request.password_bytes+1)||mlock(key,sizeof key)||mlock(secret,sizeof secret))goto end;
    if(read_all(socket,password,request.password_bytes)||read_all(socket,code,request.code_bytes)||memchr(password,0,request.password_bytes)||memchr(code,0,request.code_bytes))goto end;
    if(!getpwuid(request.uid))goto end;
    dir=open_store();if(dir<0)goto end;
    snprintf(name,sizeof name,"%u.factor",request.uid);snprintf(lock_name,sizeof lock_name,"%u.lock",request.uid);
    lock=openat(dir,lock_name,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);struct stat lock_stat;
    if(lock<0||fstat(lock,&lock_stat)||!S_ISREG(lock_stat.st_mode)||lock_stat.st_uid!=geteuid()||lock_stat.st_nlink!=1||(lock_stat.st_mode&077)||flock(lock,LOCK_EX))goto end;
    int loaded=load_record(dir,name,&record);if(loaded<0)goto end;
    if(request.operation==LOGIN_STATUS) {response.result=loaded?LOGIN_ABSENT:LOGIN_OK;goto end;}
    if(request.operation==LOGIN_ENROLL) {
        if(!loaded) {response.result=LOGIN_DENIED;goto end;}
        record.magic=LOGIN_MAGIC;record.version=1;
        if(!random_bytes(record.factor.salt,16)||!random_bytes(secret,32))goto end;
        derive(password,record.factor.salt,key);auth_wipe(password,request.password_bytes);
        memcpy(response.secret,secret,32);
        for(unsigned i=0;i<FACTOR_RECOVERY;++i) {
            uint8_t bytes[16];if(!random_bytes(bytes,sizeof bytes))goto end;
            for(unsigned j=0;j<16;++j)snprintf(response.recovery[i]+j*2,3,"%02x",bytes[j]);
            auth_wipe(bytes,sizeof bytes);factor_recovery_hash(response.recovery[i],record.factor.salt,record.factor.recovery[i]);
        }
        response.result=LOGIN_SETUP;
        if(login_write(socket,&response,sizeof response))goto cleanup;
        auth_wipe(&response,sizeof response);response.result=LOGIN_ERROR;
        uint32_t length=0;if(read_all(socket,&length,sizeof length)||length!=6||read_all(socket,code,length))goto end;code[length]=0;
        uint64_t accepted=0;
        if(!factor_check(secret,(uint64_t)time(NULL),0,code,&accepted)) {response.result=LOGIN_DENIED;goto end;}
        record.factor.last_step=accepted;
        if(!factor_seal(&record.factor,request.uid,key,secret,random_bytes)||store_record(dir,name,&record))goto end;
        response.result=LOGIN_OK;goto end;
    }
    if(loaded) {response.result=request.operation==LOGIN_VERIFY?LOGIN_ABSENT:LOGIN_DENIED;goto end;}
    uint64_t now=(uint64_t)time(NULL);
    if(now<record.retry_after) {response.result=LOGIN_RETRY;goto end;}
    derive(password,record.factor.salt,key);auth_wipe(password,request.password_bytes);
    if(!factor_open(&record.factor,request.uid,key,secret)||!second_factor(&record.factor,secret,code,now)) {
        if(record.failures<6)++record.failures;record.retry_after=now+(1u<<record.failures);
        if(!store_record(dir,name,&record))response.result=LOGIN_DENIED;goto end;
    }
    record.failures=0;record.retry_after=0;
    if(!factor_seal(&record.factor,request.uid,key,secret,random_bytes)||store_record(dir,name,&record))goto end;
    if(request.operation==LOGIN_DISABLE&&(unlinkat(dir,name,0)||fsync(dir)))goto end;
    response.result=LOGIN_OK;
end:
    /* Seed and recovery codes are returned only in the explicit SETUP response. */
    auth_wipe(response.secret,sizeof response.secret);auth_wipe(response.recovery,sizeof response.recovery);
    (void)login_write(socket,&response,sizeof response);
cleanup:
    if(password) {auth_wipe(password,(size_t)request.password_bytes+1);munlock(password,(size_t)request.password_bytes+1);free(password);}
    auth_wipe(key,sizeof key);auth_wipe(secret,sizeof secret);auth_wipe(code,sizeof code);auth_wipe(&record,sizeof record);auth_wipe(&response,sizeof response);
    munlock(key,sizeof key);munlock(secret,sizeof secret);if(lock>=0)close(lock);if(dir>=0)close(dir);
}
#ifndef MNOS_LOGIN_LIBRARY
int main(void) {
    struct passwd *service=getpwnam("login-process");if(!service||geteuid()!=service->pw_uid||geteuid()==0)return 1;
    struct rlimit no_core={0,0};if(setrlimit(RLIMIT_CORE,&no_core)||prctl(PR_SET_DUMPABLE,0))return 1;
    const char *fds=getenv("LISTEN_FDS"),*pid=getenv("LISTEN_PID");
    if(!fds||strcmp(fds,"1")||!pid||(pid_t)strtol(pid,NULL,10)!=getpid())return 1;
    signal(SIGCHLD,SIG_IGN);signal(SIGPIPE,SIG_IGN);
    for(;;) {
        int fd=accept4(3,NULL,NULL,SOCK_CLOEXEC);if(fd<0) {if(errno==EINTR)continue;return 1;}
        pid_t child=fork();if(child==0) {close(3);alarm(180);login_serve(fd);close(fd);_exit(0);}close(fd);
        if(child<0)return 1;
    }
}
#endif
