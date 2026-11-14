// SPDX-License-Identifier: GPL-3.0-or-later
#include "security-ui.hpp"
#include <openssl/hmac.h>
#include <zbar.h>
#include <cassert>
#include <ctime>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
using namespace zbar;
static unsigned phase=0,tries=0;
static std::string manual;
static std::vector<GtkWidget *> entries;
static void children(GtkWidget *widget,gpointer) {
    if(GTK_IS_ENTRY(widget))entries.push_back(widget);
    if(GTK_IS_LABEL(widget)) {
        std::string value=gtk_label_get_text(GTK_LABEL(widget));
        if(value.size()==52&&value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567")==std::string::npos)manual=value;
    }
    if(GTK_IS_CONTAINER(widget))gtk_container_foreach(GTK_CONTAINER(widget),children,nullptr);
}
static gboolean advance(gpointer) {
    assert(++tries<250);GList *windows=gtk_window_list_toplevels();
    for(GList *item=windows;item;item=item->next) {
        GtkWidget *widget=GTK_WIDGET(item->data);if(!GTK_IS_DIALOG(widget)||!gtk_widget_get_visible(widget))continue;
        std::string title=gtk_window_get_title(GTK_WINDOW(widget));entries.clear();children(widget,nullptr);
        if(phase==0&&title.find("Enable authenticator")!=std::string::npos) {
            assert(entries.size()==1);gtk_entry_set_text(GTK_ENTRY(entries[0]),"Test-only-password-491");++phase;gtk_dialog_response(GTK_DIALOG(widget),GTK_RESPONSE_ACCEPT);break;
        }
        if(phase==1&&title.find("Pair authenticator")!=std::string::npos) {
            auto *window=gtk_widget_get_window(widget);if(!window)continue;
            int width=gdk_window_get_width(window),height=gdk_window_get_height(window);assert(width<=1024&&height<=768);
            auto *pixels=gdk_pixbuf_get_from_window(window,0,0,width,height);if(!pixels)continue;
            int stride=gdk_pixbuf_get_rowstride(pixels),channels=gdk_pixbuf_get_n_channels(pixels);auto *data=gdk_pixbuf_get_pixels(pixels);
            std::vector<uint8_t> gray(size_t(width)*height);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {auto *p=data+y*stride+x*channels;gray[size_t(y)*width+x]=uint8_t((299u*p[0]+587u*p[1]+114u*p[2])/1000);}
            ImageScanner scanner;scanner.set_config(ZBAR_NONE,ZBAR_CFG_ENABLE,1);Image image(width,height,"Y800",gray.data(),gray.size());scanner.scan(image);
            std::string uri;for(auto symbol=image.symbol_begin();symbol!=image.symbol_end();++symbol)if(symbol->get_type()==ZBAR_QRCODE)uri=symbol->get_data();
            image.set_data(nullptr,0);g_object_unref(pixels);if(uri.empty())continue;
            assert(uri.find("otpauth://totp/mnOS:demo?")==0&&uri.find("secret="+manual+"&")!=std::string::npos);
            std::array<uint8_t,32> key{};const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";uint32_t value=0;unsigned bits=0,at=0;
            for(char c:manual) {value=(value<<5)|uint32_t(alphabet.find(c));bits+=5;if(bits>=8) {bits-=8;key[at++]=uint8_t(value>>bits);}}
            assert(at==32);uint64_t counter=uint64_t(time(nullptr))/30;uint8_t message[8],digest[EVP_MAX_MD_SIZE];unsigned length;
            for(unsigned i=0;i<8;++i)message[7-i]=uint8_t(counter>>(8*i));assert(HMAC(EVP_sha1(),key.data(),32,message,8,digest,&length)&&length==20);
            unsigned offset=digest[19]&15;uint32_t n=((uint32_t(digest[offset])&127)<<24)|(uint32_t(digest[offset+1])<<16)|(uint32_t(digest[offset+2])<<8)|digest[offset+3];
            char code[7];snprintf(code,sizeof code,"%06u",n%1000000);assert(entries.size()==1);gtk_entry_set_text(GTK_ENTRY(entries[0]),code);
            OPENSSL_cleanse(key.data(),key.size());OPENSSL_cleanse(uri.data(),uri.size());OPENSSL_cleanse(manual.data(),manual.size());manual.clear();
            ++phase;gtk_dialog_response(GTK_DIALOG(widget),GTK_RESPONSE_ACCEPT);break;
        }
        if(phase==2&&title.find("Save recovery codes")!=std::string::npos) {++phase;gtk_dialog_response(GTK_DIALOG(widget),GTK_RESPONSE_ACCEPT);break;}
    }
    g_list_free(windows);return phase<3?G_SOURCE_CONTINUE:G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
    assert(getenv("MNOS_AUTH_TEST_CONTAINER"));gtk_init(&argc,&argv);int child=-1,fd=login_child(1000,1,&child);assert(fd>=0);
    g_timeout_add(150,advance,nullptr);bool ok=security::configure(nullptr,fd,1000,"demo",true);shutdown(fd,SHUT_RDWR);close(fd);waitpid(child,nullptr,0);
    assert(ok&&phase==3);puts("PASS GTK installer enrollment: actual privilege broker, 1024x768 layout, screenshot QR decoded with ZBar, QR/manual key equality, independent authenticator, recovery screen");
}
