// SPDX-License-Identifier: GPL-3.0-or-later
#include "security-ui.hpp"
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
    GtkWidget *window{},*summary{},*action{};uid_t uid=getuid();std::string name;int state=LOGIN_ERROR;
    void refresh() {
        if(access("/usr/lib/mnos/login-helper",X_OK)!=0) {
            gtk_label_set_text(GTK_LABEL(summary),"精简版使用密码登录。安装完整版功能包后可开启验证器。\n命令：sudo mnos full\n说明：help -cn editions");
            gtk_button_set_label(GTK_BUTTON(action),"完整版功能尚未安装");
            gtk_widget_set_sensitive(action,false);return;
        }
        state=status(uid);
        gtk_label_set_text(GTK_LABEL(summary),state==LOGIN_OK?"已开启：密码 + 验证器验证码。\n更换密码前，请先关闭验证器；改好密码后重新绑定。":state==LOGIN_ABSENT?"尚未开启验证器。目前使用密码登录。":"无法读取认证设置。请检查 Login Process 服务。");
        gtk_button_set_label(GTK_BUTTON(action),state==LOGIN_OK?"关闭验证器":"开启验证器");
        gtk_widget_set_sensitive(action,state==LOGIN_OK||state==LOGIN_ABSENT);
    }
    Security() {
        struct passwd *p=getpwuid(uid);name=p?p->pw_name:"";
        window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"登录安全 / Login Security");gtk_window_set_default_size(GTK_WINDOW(window),600,400);
        auto *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);gtk_container_set_border_width(GTK_CONTAINER(box),28);gtk_container_add(GTK_CONTAINER(window),box);
        auto *title=security::label("登录安全 / Login Security");gtk_box_pack_start(GTK_BOX(box),title,false,false,0);
        summary=security::label("");gtk_box_pack_start(GTK_BOX(box),summary,false,false,0);
        action=gtk_button_new();gtk_box_pack_start(GTK_BOX(box),action,false,false,0);
        gtk_box_pack_start(GTK_BOX(box),security::label("验证器密钥在本机加密保存；扫码或手动输入都可以绑定。\n精简版仅提供密码验证，完整版支持额外验证。"),false,false,0);
        gtk_box_pack_start(GTK_BOX(box),security::label("人脸辅助：实验性录入与动作检测，尚未接入系统登录。"),false,false,0);
        auto *face_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),face_buttons,false,false,0);
        const char *titles[]={"录入人脸","测试人脸","删除模板"};const char *modes[]={"--enroll","--test","--delete"};
        for(unsigned i=0;i<3;++i) {
            auto *button=gtk_button_new_with_label(titles[i]);gtk_box_pack_start(GTK_BOX(face_buttons),button,true,true,0);
            gtk_widget_set_sensitive(button,access("/usr/bin/mnos-face-preview",X_OK)==0);
            g_signal_connect(button,"clicked",G_CALLBACK((+[](GtkButton *,gpointer mode) {
                GError *error=nullptr;auto *child=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,"/usr/bin/mnos-face-preview",static_cast<const char *>(mode),nullptr);
                if(child)g_object_unref(child);if(error) {security::notice(nullptr,error->message);g_error_free(error);}
            })),const_cast<char *>(modes[i]));
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
        puts("mnos-security: open your Login Security settings.\nEnroll or disable password-first TOTP using QR or manual pairing.\nDisable TOTP before changing your Linux password, then enroll again.\nCamera enrollment/testing is experimental and does not unlock the system.\nDetailed manual: help 2fa / help -cn 2fa");return 0;
    }
    const rlimit limit{0,0};if(setrlimit(RLIMIT_CORE,&limit)||prctl(PR_SET_DUMPABLE,0))return 1;
    gtk_init(&argc,&argv);Security security;gtk_main();return 0;
}
