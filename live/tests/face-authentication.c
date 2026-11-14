/* SPDX-License-Identifier: GPL-3.0-or-later */
#define main original_totp_test_main
#include "authentication.c"
#undef main
#include "face-protocol.h"
#include <sys/resource.h>
static int face_request_call(unsigned op,uid_t uid,const char *pw,const char *totp,const char *recovery,int pam){
    int pid=-1,fd=face_child(uid,pam,&pid);assert(fd>=0);struct face_response r;
    assert(!face_send(fd,op,uid,pw,totp,recovery,":99"));
    int result=login_read(fd,&r,sizeof r)?LOGIN_ERROR:(int)r.result;close(fd);waitpid(pid,NULL,0);return result;
}
static void camera_mode(const char *mode){FILE *f=fopen("/run/mnos-test-camera-mode","w");assert(f);assert(fputs(mode,f)>=0);assert(!fclose(f));}
static struct face_response enroll_face(uid_t uid,int confirm){
    int pid=-1,fd=face_child(uid,0,&pid);assert(fd>=0&&!face_send(fd,LOGIN_ENROLL,uid,password,"","",":99"));
    struct face_response setup,r;assert(!login_read(fd,&setup,sizeof setup)&&setup.result==LOGIN_SETUP);
    uint32_t answer=confirm?FACE_MAGIC:0;assert(!login_write(fd,&answer,4)&&!login_read(fd,&r,sizeof r));
    assert(r.result==(confirm?LOGIN_OK:LOGIN_DENIED));close(fd);waitpid(pid,NULL,0);return setup;
}
struct face_answers {const char *pw,*totp,*face;unsigned face_prompts;};
static int face_conversation(int n,const struct pam_message **m,struct pam_response **out,void *data){
    struct face_answers *a=data;struct pam_response *r=calloc((size_t)n,sizeof *r);assert(r);
    for(int i=0;i<n;++i)if(m[i]->msg_style==PAM_PROMPT_ECHO_OFF){
        int face=strstr(m[i]->msg,"Face")!=NULL;if(face)++a->face_prompts;
        r[i].resp=strdup(face?a->face:strstr(m[i]->msg,"Authenticator")?a->totp:a->pw);assert(r[i].resp);
    }
    *out=r;return PAM_SUCCESS;
}
static int face_pam(const char *pw,const char *totp,const char *face,unsigned *prompts){
    struct face_answers a={pw,totp,face,0};struct pam_conv conv={face_conversation,&a};pam_handle_t *pam=NULL;
    assert(pam_start("mnos-auth-test","demo",&conv,&pam)==PAM_SUCCESS);assert(pam_set_item(pam,PAM_XDISPLAY,":99")==PAM_SUCCESS);
    int result=pam_authenticate(pam,0);pam_end(pam,result);if(prompts)*prompts=a.face_prompts;return result;
}
int main(void){
    assert(geteuid()==0&&getenv("MNOS_AUTH_TEST_CONTAINER"));atexit(daemon_stop);daemon_start();
    /* Remove the earlier GUI test's disposable factor, whose recovery codes
     * were intentionally not exported. Never run this test on a real system. */
    assert(!unlink(MNOS_LOGIN_STORE "/1000.factor"));
    camera_mode("pass");
    assert(face_request_call(LOGIN_STATUS,1000,"","","",1)==LOGIN_ABSENT);
    assert(face_request_call(LOGIN_ENROLL,1000,"wrong","","",0)==LOGIN_DENIED);
    assert(face_request_call(LOGIN_ENROLL,1000,password,"","",1)==LOGIN_DENIED); /* read-only PAM mode */
    struct face_response cancelled=enroll_face(1001,0);OPENSSL_cleanse(&cancelled,sizeof cancelled);
    assert(face_request_call(LOGIN_STATUS,1001,"","","",1)==LOGIN_ABSENT);
    struct face_response setup=enroll_face(1000,1);
    assert(face_request_call(LOGIN_STATUS,1000,"","","",1)==LOGIN_OK);
    unsigned prompts=0;assert(face_pam("wrong","","",&prompts)!=PAM_SUCCESS&&prompts==0);
    assert(face_pam(password,"","",&prompts)==PAM_SUCCESS&&prompts==1);
    camera_mode("fail");assert(face_pam(password,"","",NULL)!=PAM_SUCCESS);
    assert(face_pam(password,"",setup.recovery[0],NULL)==PAM_SUCCESS); /* camera backoff cannot lock out recovery */
    assert(face_pam(password,"",setup.recovery[0],NULL)!=PAM_SUCCESS);
    assert(face_pam(password,"",setup.recovery[1],NULL)==PAM_SUCCESS);
    /* Corrupt/missing content never turns an enrolled factor into password-only. */
    assert(!rename(MNOS_FACE_STORE "/1000.face",MNOS_FACE_STORE "/saved.face"));assert(!mkdir(MNOS_FACE_STORE "/1000.face",0700));
    assert(face_pam(password,"",setup.recovery[2],NULL)!=PAM_SUCCESS);
    assert(!rmdir(MNOS_FACE_STORE "/1000.face")&&!rename(MNOS_FACE_STORE "/saved.face",MNOS_FACE_STORE "/1000.face"));
    assert(face_pam(password,"",setup.recovery[2],NULL)==PAM_SUCCESS);
    /* Reject authentication if recording a recovery use cannot reach disk.
     * RLIMIT_FSIZE applies only to this disposable child and its helper. */
    pid_t no_space=fork();assert(no_space>=0);if(!no_space){
        struct rlimit limit={0,0};assert(!setrlimit(RLIMIT_FSIZE,&limit));
        assert(face_request_call(LOGIN_VERIFY,1000,password,"",setup.recovery[4],1)!=LOGIN_OK);_exit(0);
    }
    int no_space_status;assert(waitpid(no_space,&no_space_status,0)==no_space&&WIFEXITED(no_space_status)&&!WEXITSTATUS(no_space_status));
    assert(face_pam(password,"",setup.recovery[4],NULL)==PAM_SUCCESS);
    pid_t other=fork();assert(other>=0);if(!other){
        assert(!setgroups(0,NULL)&&!setgid(1001)&&!setuid(1001));assert(open(MNOS_FACE_STORE "/1000.face",O_RDONLY)<0);
        int pid=-1,fd=face_child(1000,1,&pid);assert(fd>=0);(void)face_send(fd,LOGIN_STATUS,1000,"","","","");struct face_response r;assert(login_read(fd,&r,sizeof r));close(fd);waitpid(pid,NULL,0);_exit(0);
    }
    int ws;assert(waitpid(other,&ws,0)==other&&WIFEXITED(ws)&&!WEXITSTATUS(ws));
    /* Use a fresh genuine TOTP enrollment to prove face success cannot replace
     * the required authenticator in common-auth. */
    int child=-1,fd=login_child(1000,0,&child);assert(fd>=0&&!login_send(fd,LOGIN_ENROLL,1000,password,""));
    struct login_response factor,finish;assert(!login_read(fd,&factor,sizeof factor)&&factor.result==LOGIN_SETUP);
    char otp[7];oracle(factor.secret,otp);uint32_t size=6;assert(!login_write(fd,&size,4)&&!login_write(fd,otp,6));
    assert(!login_read(fd,&finish,sizeof finish)&&finish.result==LOGIN_OK);close(fd);waitpid(child,NULL,0);
    camera_mode("pass");assert(face_pam(password,"incorrect","",NULL)!=PAM_SUCCESS);wait_retry();
    assert(face_pam(password,factor.recovery[0],"",NULL)==PAM_SUCCESS);
    /* Locked-screen clients may use PAM without euid root. They can only
     * verify their own face, never use the PAM path to enroll/remove factors. */
    pid_t own=fork();assert(own>=0);if(!own){assert(!setgroups(0,NULL)&&!setgid(1000)&&!setuid(1000));assert(face_pam(password,factor.recovery[1],"",NULL)==PAM_SUCCESS);_exit(0);}
    assert(waitpid(own,&ws,0)==own&&WIFEXITED(ws)&&!WEXITSTATUS(ws));
    assert(face_request_call(LOGIN_DISABLE,1000,password,factor.recovery[2],setup.recovery[3],0)==LOGIN_OK);
    assert(face_request_call(LOGIN_STATUS,1000,"","","",1)==LOGIN_ABSENT);
    assert(request(LOGIN_DISABLE,1000,password,factor.recovery[3])==LOGIN_OK);
    pid_t gui=fork();assert(gui>=0);if(!gui){assert(!setgroups(0,NULL)&&!setgid(1000)&&!setuid(1000));execl("/tmp/face-ui-test","face-ui-test",(char *)0);_exit(127);}
    assert(waitpid(gui,&ws,0)==gui&&WIFEXITED(ws)&&!WEXITSTATUS(ws));
    OPENSSL_cleanse(&factor,sizeof factor);OPENSSL_cleanse(&setup,sizeof setup);
    puts("PASS real face broker + PAM: password-first, cancel/enroll/verify/recovery/remove, one-use recovery, unavailable camera, corrupt record, cross-user denial, unprivileged screen-lock client, independent TOTP still required. Camera success uses an explicitly isolated test fixture, not a physical-camera claim.");
}
