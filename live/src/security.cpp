// SPDX-License-Identifier: GPL-3.0-or-later
#include "security-ui.hpp"
extern "C" {
#include "face-protocol.h"
}
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
static int status(uid_t uid) {
    int pid=-1,fd=login_child(uid,0,&pid);if(fd<0)return LOGIN_ERROR;
    struct login_response r{};
    if(login_send(fd,LOGIN_STATUS,uid,"","")||login_read(fd,&r,sizeof r))r.result=LOGIN_ERROR;
    close(fd);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}return int(r.result);
}
struct Security {
    GtkWidget *window{},*summary{},*action{},*face_summary{};uid_t uid=getuid();std::string name;int state=LOGIN_ERROR;
    void refresh() {
        int pid=-1,fd=face_child(uid,0,&pid);face_response r{};int face=LOGIN_ERROR;
        if(fd>=0) {if(!face_send(fd,LOGIN_STATUS,uid,"","","","")&&!login_read(fd,&r,sizeof r))face=int(r.result);close(fd);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}}
        gtk_label_set_text(GTK_LABEL(face_summary),face==LOGIN_OK?mnos_text("Face verification enabled: additional to password and any enabled TOTP.","人脸验证已开启：密码和已开启的 TOTP 之后，还需人脸或人脸恢复码。"):face==LOGIN_ABSENT?mnos_text("Face verification is off. Enroll to enable; keep the recovery codes offline.","人脸验证未开启。录入后可启用，请离线保存人脸恢复码。"):mnos_text("Face verification is unavailable. Full authentication components are required.","人脸验证不可用，需要安装完整版认证组件。"));
        if(access("/usr/lib/mnos/login-helper",X_OK)!=0) {
            gtk_label_set_text(GTK_LABEL(summary),i18n::text("精简版使用密码登录。安装完整版功能包后可开启验证器。\n命令：sudo mnos full\n说明：help -cn editions"));
            gtk_button_set_label(GTK_BUTTON(action),i18n::text("完整版功能尚未安装"));
            gtk_widget_set_sensitive(action,false);return;
        }
        state=status(uid);
        gtk_label_set_text(GTK_LABEL(summary),state==LOGIN_OK?i18n::text("已开启：密码 + 验证器验证码。\n更换密码前，请先关闭验证器；改好密码后重新绑定。"):state==LOGIN_ABSENT?mnos_text("Authenticator verification is off.","验证器验证未开启。"):i18n::text("无法读取认证设置。请检查 Login Process 服务。"));
        gtk_button_set_label(GTK_BUTTON(action),state==LOGIN_OK?i18n::text("关闭验证器"):i18n::text("开启验证器"));
        gtk_widget_set_sensitive(action,state==LOGIN_OK||state==LOGIN_ABSENT);
    }
    Security() {
        struct passwd *p=getpwuid(uid);name=p?p->pw_name:"";
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),i18n::text("登录安全 / Login Security"));gtk_window_set_default_size(GTK_WINDOW(window),600,400);
        auto *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);gtk_container_set_border_width(GTK_CONTAINER(box),28);gtk_container_add(GTK_CONTAINER(window),box);
        auto *title=security::label(i18n::text("登录安全 / Login Security"));gtk_box_pack_start(GTK_BOX(box),title,false,false,0);
        summary=security::label("");gtk_box_pack_start(GTK_BOX(box),summary,false,false,0);
        action=gtk_button_new();gtk_box_pack_start(GTK_BOX(box),action,false,false,0);
        gtk_box_pack_start(GTK_BOX(box),security::label(i18n::text("验证器密钥在本机加密保存；扫码或手动输入都可以绑定。\n精简版仅提供密码验证，完整版支持额外验证。")),false,false,0);
        face_summary=security::label("");gtk_box_pack_start(GTK_BOX(box),face_summary,false,false,0);
        gtk_box_pack_start(GTK_BOX(box),security::label(mnos_text("Ordinary RGB camera: no trusted depth; sophisticated replay remains a risk. Local X11 camera window or separate one-use recovery code.","普通 RGB 摄像头没有可信深度，仍可能被精心制作的重放欺骗。使用本地 X11 相机窗口或单独的一次性恢复码。")),false,false,0);
        auto *face_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),face_buttons,false,false,0);
        const char *titles[]={i18n::text("录入人脸"),i18n::text("测试人脸"),mnos_text("Disable face verification","关闭人脸验证")};const char *modes[]={"--enroll","--test","--delete"};
        for(unsigned i=0;i<3;++i) {
            auto *button=gtk_button_new_with_label(titles[i]);gtk_box_pack_start(GTK_BOX(face_buttons),button,true,true,0);
            gtk_widget_set_sensitive(button,access("/usr/bin/mnos-face-preview",X_OK)==0);
            g_object_set_data(G_OBJECT(button),"mode",const_cast<char *>(modes[i]));
            g_signal_connect(button,"clicked",G_CALLBACK((+[](GtkButton *button,gpointer data) {
                auto *s=static_cast<Security *>(data);const char *mode=static_cast<const char *>(g_object_get_data(G_OBJECT(button),"mode"));
                GError *error=nullptr;auto *child=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,"/usr/bin/mnos-face-preview",mode,nullptr);
                if(child){g_subprocess_wait_async(child,nullptr,+[](GObject *object,GAsyncResult *result,gpointer data){GError *error=nullptr;g_subprocess_wait_finish(G_SUBPROCESS(object),result,&error);if(error)g_error_free(error);static_cast<Security *>(data)->refresh();g_object_unref(object);},s);}
                if(error){security::notice(GTK_WINDOW(s->window),error->message);g_error_free(error);}
            })),this);
        }
        g_signal_connect_swapped(action,"clicked",G_CALLBACK((+[](Security *s) {
            int pid=-1,fd=login_child(s->uid,0,&pid);if(fd<0)return;
            security::configure(GTK_WINDOW(s->window),fd,s->uid,s->name,s->state==LOGIN_ABSENT);
            shutdown(fd,SHUT_RDWR);close(fd);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}s->refresh();
        })),this);
        g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),nullptr);refresh();gtk_widget_show_all(window);
    }
};
int main(int argc,char **argv) {
    if(argc>1&&(!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))) {
        puts("mnos-security: open your Login Security settings.\nEnroll or disable password-first TOTP using QR or manual pairing.\nDisable TOTP before changing your Linux password, then enroll again.\nFace verification adds a local camera or one-use face recovery check after password and any enabled TOTP.\nOrdinary RGB cameras are not trusted-depth hardware.\nDetailed manual: help 2fa / help -cn 2fa");return 0;
    }
    const rlimit limit{0,0};if(setrlimit(RLIMIT_CORE,&limit)||prctl(PR_SET_DUMPABLE,0))return 1;
    gtk_init(&argc,&argv);Security security;gtk_main();return 0;
}
