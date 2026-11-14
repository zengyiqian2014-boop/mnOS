// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gtk/gtk.h>
#include <string>
#include <array>
#include <algorithm>
#include <sys/socket.h>
extern "C" {
#include "login-protocol.h"
#include "factor.h"
#include "auth.h"
#include "qrcodegen.h"
}
namespace security {
inline GtkWidget *label(const char *text) {
    auto *widget=gtk_label_new(text);gtk_label_set_xalign(GTK_LABEL(widget),0);
    gtk_label_set_line_wrap(GTK_LABEL(widget),true);return widget;
}
inline void notice(GtkWindow *parent,const char *text) {
    auto *dialog=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL,GTK_MESSAGE_INFO,GTK_BUTTONS_CLOSE,"%s",text);
    gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);
}
inline GtkWidget *dialog(GtkWindow *parent,const char *title,const char *action) {
    auto *d=gtk_dialog_new_with_buttons(title,parent,GTK_DIALOG_MODAL,"取消",GTK_RESPONSE_CANCEL,action,GTK_RESPONSE_ACCEPT,nullptr);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))),24);
    gtk_window_set_default_size(GTK_WINDOW(d),520,-1);gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return d;
}
inline std::string quote(const std::string &text) {
    std::string escaped;const char *hex="0123456789ABCDEF";
    for(unsigned char c:text)if(g_ascii_isalnum(c)||c=='-'||c=='_'||c=='.')escaped+=char(c);else {escaped+='%';escaped+=hex[c>>4];escaped+=hex[c&15];}
    return escaped;
}
inline bool configure(GtkWindow *parent,int fd,uint32_t uid,const std::string &name,bool enable) {
    auto *d=dialog(parent,enable?"开启验证器 / Enable authenticator":"关闭验证器 / Disable authenticator","继续");
    auto *box=gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_box_pack_start(GTK_BOX(box),label("输入此账号的密码。密码不会保存到安装日志或命令行。"),false,false,8);
    auto *password=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(password),false);gtk_entry_set_activates_default(GTK_ENTRY(password),true);
    gtk_box_pack_start(GTK_BOX(box),password,false,false,8);
    GtkWidget *code=nullptr;
    if(!enable) {
        gtk_box_pack_start(GTK_BOX(box),label("再输入验证器验证码或一次性恢复码。"),false,false,8);
        code=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(code),false);gtk_entry_set_activates_default(GTK_ENTRY(code),true);
        gtk_box_pack_start(GTK_BOX(box),code,false,false,8);
    }
    gtk_widget_show_all(d);gtk_widget_grab_focus(password);
    if(gtk_dialog_run(GTK_DIALOG(d))!=GTK_RESPONSE_ACCEPT) {gtk_entry_set_text(GTK_ENTRY(password),"");gtk_widget_destroy(d);shutdown(fd,SHUT_RDWR);return false;}
    bool sent=!login_send(fd,enable?LOGIN_ENROLL:LOGIN_DISABLE,uid,gtk_entry_get_text(GTK_ENTRY(password)),code?gtk_entry_get_text(GTK_ENTRY(code)):"");
    gtk_entry_set_text(GTK_ENTRY(password),"");if(code)gtk_entry_set_text(GTK_ENTRY(code),"");gtk_widget_destroy(d);
    struct login_response response{};
    if(!sent||login_read(fd,&response,sizeof response)) {notice(parent,"无法完成认证。设置未完成，请重试。");return false;}
    if(enable&&response.result==LOGIN_SETUP) {
        std::array<uint8_t,qrcodegen_BUFFER_LEN_MAX> qr{},temporary{};
        char key[53];factor_base32(response.secret,key);
        std::string uri="otpauth://totp/mnOS:"+quote(name)+"?secret="+key+"&issuer=mnOS&algorithm=SHA1&digits=6&period=30";
        if(!qrcodegen_encodeText(uri.c_str(),temporary.data(),qr.data(),qrcodegen_Ecc_MEDIUM,1,20,qrcodegen_Mask_AUTO,true)) {
            auth_wipe(&response,sizeof response);auth_wipe(key,sizeof key);auth_wipe(uri.data(),uri.size());shutdown(fd,SHUT_RDWR);return false;
        }
        auth_wipe(temporary.data(),temporary.size());
        d=dialog(parent,"绑定验证器 / Pair authenticator","验证并开启");box=gtk_dialog_get_content_area(GTK_DIALOG(d));
        gtk_box_pack_start(GTK_BOX(box),label("方式一：用验证器扫描二维码。二维码只在本机生成。"),false,false,8);
        auto *drawing=gtk_drawing_area_new();gtk_widget_set_size_request(drawing,260,260);
        g_signal_connect(drawing,"draw",G_CALLBACK((+[](GtkWidget *w,cairo_t *cr,gpointer data)->gboolean {
            auto *q=static_cast<uint8_t *>(data);int n=qrcodegen_getSize(q);
            int size=std::min(gtk_widget_get_allocated_width(w),gtk_widget_get_allocated_height(w));int scale=size/(n+8);
            int x=(gtk_widget_get_allocated_width(w)-(n+8)*scale)/2,y=(gtk_widget_get_allocated_height(w)-(n+8)*scale)/2;
            cairo_set_source_rgb(cr,1,1,1);cairo_rectangle(cr,x,y,(n+8)*scale,(n+8)*scale);cairo_fill(cr);
            cairo_set_source_rgb(cr,0,0,0);
            for(int row=0;row<n;++row)for(int col=0;col<n;++col)if(qrcodegen_getModule(q,col,row))cairo_rectangle(cr,x+(col+4)*scale,y+(row+4)*scale,scale,scale);
            cairo_fill(cr);return false;
        })),qr.data());
        gtk_box_pack_start(GTK_BOX(box),drawing,false,false,0);
        gtk_box_pack_start(GTK_BOX(box),label("方式二：手动填写下方密钥；选择基于时间、SHA-1、6 位、30 秒。"),false,false,8);
        auto *manual=label(key);gtk_label_set_selectable(GTK_LABEL(manual),false);gtk_label_set_line_wrap_mode(GTK_LABEL(manual),PANGO_WRAP_CHAR);
        gtk_box_pack_start(GTK_BOX(box),manual,false,false,8);
        gtk_box_pack_start(GTK_BOX(box),label("输入验证器当前显示的六位验证码："),false,false,8);
        code=gtk_entry_new();gtk_entry_set_input_purpose(GTK_ENTRY(code),GTK_INPUT_PURPOSE_DIGITS);gtk_entry_set_visibility(GTK_ENTRY(code),false);
        gtk_entry_set_activates_default(GTK_ENTRY(code),true);gtk_box_pack_start(GTK_BOX(box),code,false,false,8);
        gtk_widget_show_all(d);gtk_widget_grab_focus(code);
        bool accepted=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;
        std::string value=gtk_entry_get_text(GTK_ENTRY(code));gtk_entry_set_text(GTK_ENTRY(code),"");gtk_label_set_text(GTK_LABEL(manual),"");gtk_widget_destroy(d);
        auth_wipe(key,sizeof key);auth_wipe(uri.data(),uri.size());auth_wipe(qr.data(),qr.size());
        bool sent_code=false;
        if(accepted&&value.size()==6&&value.find_first_not_of("0123456789")==std::string::npos) {
            uint32_t length=6;sent_code=!login_write(fd,&length,sizeof length)&&!login_write(fd,value.data(),length);
        }
        auth_wipe(value.data(),value.size());
        struct login_response result{};
        if(!sent_code||login_read(fd,&result,sizeof result)||result.result!=LOGIN_OK) {
            auth_wipe(&response,sizeof response);shutdown(fd,SHUT_RDWR);notice(parent,"验证器尚未开启。请检查验证码和系统时间后重新绑定。");return false;
        }
        d=dialog(parent,"保存恢复码 / Save recovery codes","已保存");box=gtk_dialog_get_content_area(GTK_DIALOG(d));
        gtk_box_pack_start(GTK_BOX(box),label("验证器已开启。请在另一处离线保存以下恢复码，每个只能使用一次，仍需先输入密码。关闭窗口后不会再次显示。"),false,false,8);
        std::string recovery;
        for(unsigned i=0;i<8;++i) {recovery+=response.recovery[i];recovery+='\n';}
        auto *codes=label(recovery.c_str());gtk_box_pack_start(GTK_BOX(box),codes,false,false,8);gtk_widget_show_all(d);gtk_dialog_run(GTK_DIALOG(d));
        gtk_label_set_text(GTK_LABEL(codes),"");gtk_widget_destroy(d);auth_wipe(recovery.data(),recovery.size());auth_wipe(&response,sizeof response);return true;
    }
    bool ok=response.result==LOGIN_OK;auth_wipe(&response,sizeof response);
    notice(parent,ok?"验证器已关闭。下次登录使用密码。":"未能更改设置。请检查密码和验证码，稍后重试。");return ok;
}
}
