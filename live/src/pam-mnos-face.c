/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "face-protocol.h"
#include "mnos-locale.h"
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int request(uid_t uid,unsigned operation,const char *password,const char *code,const char *display) {
    int pid=-1,fd=face_child(uid,1,&pid);if(fd<0)return LOGIN_ERROR;
    struct face_response r={.result=LOGIN_ERROR};
    if(face_send(fd,operation,uid,password,"",code,display)||login_read(fd,&r,sizeof r))r.result=LOGIN_ERROR;
    close(fd);while(waitpid(pid,NULL,0)<0&&errno==EINTR){}return (int)r.result;
}
static int account(pam_handle_t *pam,uid_t *uid) {
    const char *name=NULL;if(pam_get_user(pam,&name,NULL)!=PAM_SUCCESS||!name)return PAM_USER_UNKNOWN;
    struct passwd *user=getpwnam(name);if(!user)return PAM_USER_UNKNOWN;*uid=user->pw_uid;return PAM_SUCCESS;
}
PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pam,int flags,int argc,const char **argv) {
    (void)flags;(void)argc;(void)argv;uid_t uid;
    if(account(pam,&uid)!=PAM_SUCCESS)return PAM_USER_UNKNOWN;
    int state=request(uid,LOGIN_STATUS,"","","");if(state==LOGIN_ABSENT)return PAM_SUCCESS;if(state!=LOGIN_OK)return PAM_AUTH_ERR;
    const char *password=NULL;if(pam_get_authtok(pam,PAM_AUTHTOK,&password,NULL)!=PAM_SUCCESS||!password)return PAM_AUTH_ERR;
    const void *item=NULL;const char *display="";
    if(pam_get_item(pam,PAM_XDISPLAY,&item)==PAM_SUCCESS&&item)display=item;
    if(!*display){const char *local=pam_getenv(pam,"DISPLAY");if(!local)local=getenv("DISPLAY");if(local)display=local;}
    char *code=NULL;
    const char *prompt=mnos_text("Face verification: press Enter for camera, or enter a face recovery code: ","人脸验证：直接回车使用摄像头，或输入人脸恢复码：");
    if(pam_prompt(pam,PAM_PROMPT_ECHO_OFF,&code,"%s",prompt)!=PAM_SUCCESS||!code)return PAM_AUTH_ERR;
    int used_camera=!*code;
    int result=request(uid,LOGIN_VERIFY,password,code,display);explicit_bzero(code,strlen(code));free(code);code=NULL;
    if(result!=LOGIN_OK&&used_camera){
        pam_error(pam,"%s",mnos_text("Camera verification failed or is unavailable. Use a saved one-use face recovery code.","摄像头验证未通过或不可用。可以输入已保存的一次性人脸恢复码。"));
        if(pam_prompt(pam,PAM_PROMPT_ECHO_OFF,&code,"%s",mnos_text("Face recovery code (32 characters): ","人脸恢复码（32 位）："))!=PAM_SUCCESS||!code)return PAM_AUTH_ERR;
        if(strlen(code)==32)result=request(uid,LOGIN_VERIFY,password,code,"");
        explicit_bzero(code,strlen(code));free(code);
    }
    if(result==LOGIN_RETRY)pam_error(pam,"%s",mnos_text("Please wait before trying again.","请稍后重试。"));
    return result==LOGIN_OK?PAM_SUCCESS:PAM_AUTH_ERR;
}
PAM_EXTERN int pam_sm_setcred(pam_handle_t *p,int f,int a,const char **v){(void)p;(void)f;(void)a;(void)v;return PAM_SUCCESS;}
PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pam,int flags,int argc,const char **argv){
    (void)flags;(void)argc;(void)argv;uid_t uid;if(account(pam,&uid)!=PAM_SUCCESS)return PAM_USER_UNKNOWN;
    if(request(uid,LOGIN_STATUS,"","","")==LOGIN_ABSENT)return PAM_SUCCESS;
    pam_error(pam,"%s",mnos_text("Disable face verification in Login Security before changing your password, then enroll again.","更改密码前，请先在登录安全设置中关闭人脸验证，改好后重新录入。"));return PAM_AUTHTOK_ERR;
}
