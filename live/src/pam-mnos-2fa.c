/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "login-protocol.h"
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
static int request(unsigned op,uid_t uid,const char *password,const char *code) {
    int child=-1,fd=login_child(uid,0,&child);if(fd<0)return LOGIN_ERROR;
    struct login_response result={.result=LOGIN_ERROR};
    if(login_send(fd,op,uid,password,code)||login_read(fd,&result,sizeof result))result.result=LOGIN_ERROR;
    close(fd);while(waitpid(child,NULL,0)<0&&errno==EINTR){}return (int)result.result;
}
static int account(pam_handle_t *pam,uid_t *uid) {
    const char *name=NULL;if(pam_get_user(pam,&name,NULL)!=PAM_SUCCESS||!name)return PAM_USER_UNKNOWN;
    struct passwd *user=getpwnam(name);if(!user)return PAM_USER_UNKNOWN;*uid=user->pw_uid;return PAM_SUCCESS;
}
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pam,int flags,int argc,const char **argv) {
    (void)flags;(void)argc;(void)argv;uid_t uid;
    if(account(pam,&uid)!=PAM_SUCCESS)return PAM_USER_UNKNOWN;
    int configured=request(LOGIN_STATUS,uid,"","");
    if(configured==LOGIN_ABSENT)return PAM_SUCCESS;
    if(configured!=LOGIN_OK)return PAM_AUTH_ERR;
    const char *password=NULL;
    if(pam_get_authtok(pam,PAM_AUTHTOK,&password,NULL)!=PAM_SUCCESS||!password)return PAM_AUTH_ERR;
    char *code=NULL;
    if(pam_prompt(pam,PAM_PROMPT_ECHO_OFF,&code,"Authenticator code or recovery code: ")!=PAM_SUCCESS||!code)return PAM_AUTH_ERR;
    int result=request(LOGIN_VERIFY,uid,password,code);explicit_bzero(code,strlen(code));free(code);
    if(result==LOGIN_RETRY)pam_error(pam,"Please wait before trying another verification code.");
    return result==LOGIN_OK?PAM_SUCCESS:PAM_AUTH_ERR;
}
PAM_EXTERN int pam_sm_setcred(pam_handle_t *p,int f,int a,const char **v) {(void)p;(void)f;(void)a;(void)v;return PAM_SUCCESS;}
PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pam,int flags,int argc,const char **argv) {
    (void)flags;(void)argc;(void)argv;uid_t uid;
    if(account(pam,&uid)!=PAM_SUCCESS)return PAM_USER_UNKNOWN;
    int result=request(LOGIN_STATUS,uid,"","");
    if(result==LOGIN_ABSENT)return PAM_SUCCESS;
    pam_error(pam,"Disable authenticator verification in mnOS Login Security before changing this password, then enable it again.");
    return PAM_AUTHTOK_ERR;
}
