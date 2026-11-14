// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary-user settings client. Recognition and credential storage are never
// trusted to this GUI; the broker runs its own isolated camera worker.
#include "security-ui.hpp"
extern "C" {
#include "face-protocol.h"
}
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
static const char *tr(const char *en,const char *zh){return mnos_text(en,zh);}
static void reap(int pid){while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}}
static int status(bool face) {
    int pid=-1,fd=face?face_child(getuid(),0,&pid):login_child(getuid(),0,&pid);if(fd<0)return LOGIN_ERROR;
    int result=LOGIN_ERROR;
    if(face){face_response r{};if(!face_send(fd,LOGIN_STATUS,getuid(),"","","","")&&!login_read(fd,&r,sizeof r))result=int(r.result);}
    else {login_response r{};if(!login_send(fd,LOGIN_STATUS,getuid(),"","")&&!login_read(fd,&r,sizeof r))result=int(r.result);}
    close(fd);reap(pid);return result;
}
static bool configure(unsigned op) {
    int state=status(true),factor=status(false);
    if((state!=LOGIN_OK&&state!=LOGIN_ABSENT)||(factor!=LOGIN_OK&&factor!=LOGIN_ABSENT)){
        security::notice(nullptr,tr("Authentication service is unavailable. No setting was changed.","认证服务不可用。设置未更改。"));return false;
    }
    if((op==LOGIN_ENROLL&&state==LOGIN_OK)||(op!=LOGIN_ENROLL&&state!=LOGIN_OK)){
        security::notice(nullptr,tr("Disable the current face profile before enrolling again; enroll a profile before testing or removing it.","重新录入前请先关闭现有人脸验证；测试或关闭前需要先录入。"));return false;
    }
    auto *d=security::dialog(nullptr,tr("Face verification","人脸验证"),tr("Continue","继续"));auto *box=gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_box_pack_start(GTK_BOX(box),security::label(tr("Your password is always required. Enabled TOTP remains required too. A separate set of one-use recovery codes works when the camera is unavailable.","始终需要账号密码；已开启的 TOTP 仍需验证。相机不可用时可使用单独的一次性人脸恢复码。")),false,false,8);
    GtkWidget *consent=nullptr;
    if(op==LOGIN_ENROLL){
        gtk_box_pack_start(GTK_BOX(box),security::label(tr("Ordinary RGB cameras cannot measure trusted 3D depth. Blink, head-turn and color checks can still be fooled by sophisticated replay. Face verification will apply to login, unlocking and privilege authentication. Non-graphical sessions require a face recovery code.","普通 RGB 摄像头没有可信的 3D 深度。眨眼、转头和颜色检查仍可能被精心制作的重放欺骗。开启后将用于登录、解锁和提权认证；非图形会话需要人脸恢复码。")),false,false,8);
        consent=gtk_check_button_new_with_label(tr("Enable additional face verification with these limitations","了解上述限制，启用附加人脸验证"));gtk_box_pack_start(GTK_BOX(box),consent,false,false,8);
    }
    gtk_box_pack_start(GTK_BOX(box),security::label(tr("Account password","账号密码")),false,false,4);
    auto *password=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(password),false);gtk_box_pack_start(GTK_BOX(box),password,false,false,4);
    GtkWidget *totp=nullptr,*recovery=nullptr;
    if(factor==LOGIN_OK){gtk_box_pack_start(GTK_BOX(box),security::label(tr("Authenticator code or TOTP recovery code","验证器验证码或 TOTP 恢复码")),false,false,4);totp=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(totp),false);gtk_box_pack_start(GTK_BOX(box),totp,false,false,4);}
    if(op!=LOGIN_ENROLL){gtk_box_pack_start(GTK_BOX(box),security::label(tr("Face recovery code (leave empty to use the camera)","人脸恢复码（留空使用摄像头）")),false,false,4);recovery=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(recovery),false);gtk_box_pack_start(GTK_BOX(box),recovery,false,false,4);}
    if(consent){gtk_dialog_set_response_sensitive(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT,false);g_signal_connect(consent,"toggled",G_CALLBACK((+[](GtkToggleButton *button,gpointer dialog){gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,gtk_toggle_button_get_active(button));})),d);}
    gtk_widget_show_all(d);gtk_widget_grab_focus(password);
    bool accepted=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT&&(!consent||gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(consent)));
    int pid=-1,fd=-1;bool sent=false;
    if(accepted){fd=face_child(getuid(),0,&pid);sent=fd>=0&&!face_send(fd,op,getuid(),gtk_entry_get_text(GTK_ENTRY(password)),totp?gtk_entry_get_text(GTK_ENTRY(totp)):"",recovery?gtk_entry_get_text(GTK_ENTRY(recovery)):"",getenv("DISPLAY"));}
    gtk_entry_set_text(GTK_ENTRY(password),"");if(totp)gtk_entry_set_text(GTK_ENTRY(totp),"");if(recovery)gtk_entry_set_text(GTK_ENTRY(recovery),"");gtk_widget_destroy(d);
    if(!accepted)return false;
    face_response response{};bool ok=sent&&!login_read(fd,&response,sizeof response);
    if(ok&&response.result==LOGIN_SETUP&&op==LOGIN_ENROLL){
        d=security::dialog(nullptr,tr("Save face recovery codes","保存人脸恢复码"),tr("Saved; enable face verification","已保存，启用人脸验证"));box=gtk_dialog_get_content_area(GTK_DIALOG(d));
        gtk_box_pack_start(GTK_BOX(box),security::label(tr("Keep these codes offline, outside this computer. Each replaces one face check, still requires your password and any enabled TOTP, and works only once. Cancelling leaves face verification disabled.","请将这些码离线保存到此电脑之外。每个码只能代替一次人脸验证，仍需密码和已开启的 TOTP。取消会保持人脸验证关闭。")),false,false,8);
        std::string codes;for(auto &code:response.recovery){codes+=code;codes+='\n';}
        auto *text=security::label(codes.c_str());gtk_box_pack_start(GTK_BOX(box),text,false,false,8);gtk_widget_show_all(d);
        accepted=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;gtk_label_set_text(GTK_LABEL(text),"");gtk_widget_destroy(d);auth_wipe(codes.data(),codes.size());auth_wipe(&response,sizeof response);
        uint32_t confirm=accepted?FACE_MAGIC:0;ok=!login_write(fd,&confirm,sizeof confirm)&&!login_read(fd,&response,sizeof response);
    }
    if(fd>=0){shutdown(fd,SHUT_RDWR);close(fd);reap(pid);}unsigned result=ok?response.result:unsigned(LOGIN_ERROR);auth_wipe(&response,sizeof response);
    if(result==LOGIN_OK)security::notice(nullptr,op==LOGIN_ENROLL?tr("Face verification is now enabled for this account. The encrypted profile is stored by Login Process.","此账号已启用人脸验证。加密模板由 Login Process 保管。"):op==LOGIN_DISABLE?tr("Face verification is disabled. Password and any enabled TOTP are unchanged.","人脸验证已关闭。密码和已开启的 TOTP 保持不变。"):tr("Authentication passed through the same backend used at login.","认证通过，使用的是登录时的同一验证后端。"));
    else security::notice(nullptr,result==LOGIN_RETRY?tr("Wait before trying again. Use a saved face recovery code if your camera is unavailable.","请稍后重试。相机不可用时可使用已保存的人脸恢复码。"):tr("Authentication or setup did not complete. Check your password, codes, local X11 display and camera. No login was authorized.","认证或设置未完成。请检查密码、验证码、本地 X11 显示与摄像头。此次未授权登录。"));
    return result==LOGIN_OK;
}
int main(int argc,char **argv){
    if(argc==2&&(!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))){puts("mnos-face-preview --enroll | --test | --delete\nPassword-first face authentication, additional to any enabled TOTP.\nRGB camera, local X11 display and forwarding to the guest are required.\nOne-use face recovery codes support unavailable cameras and text sessions.\nOrdinary cameras are not trusted-depth hardware; advanced replay remains a risk.\nManual: help 2fa / help -cn 2fa");return 0;}
    rlimit core{0,0};if(setrlimit(RLIMIT_CORE,&core)||prctl(PR_SET_DUMPABLE,0)||getuid()<1000)return 1;
    gtk_init(&argc,&argv);const char *mode=argc==2?argv[1]:"";
    if(!strcmp(mode,"--first-run")){
        char path[96];snprintf(path,sizeof path,"/var/lib/mnos/face-setup/%u",getuid());struct stat st{};
        if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022)||status(true)==LOGIN_OK)return 0;
        // This is only an invitation to enroll. Cancelling must not enable a factor.
        mode="--enroll";
    }
    unsigned op=!strcmp(mode,"--enroll")?LOGIN_ENROLL:!strcmp(mode,"--test")?LOGIN_VERIFY:!strcmp(mode,"--delete")?LOGIN_DISABLE:0;
    if(!op)return 2;return configure(op)?0:1;
}
