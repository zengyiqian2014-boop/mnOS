// SPDX-License-Identifier: GPL-3.0-or-later
#define main face_settings_program
#include "face-preview.cpp"
#undef main
#include <cassert>
#include <vector>
static unsigned phase=0,ticks=0;
static bool removing=false;
static std::string saved_recovery;
static std::vector<GtkWidget *> fields,checks,labels;
static void collect(GtkWidget *w,gpointer){
    if(GTK_IS_ENTRY(w))fields.push_back(w);
    if(GTK_IS_CHECK_BUTTON(w))checks.push_back(w);
    if(GTK_IS_LABEL(w))labels.push_back(w);
    if(GTK_IS_CONTAINER(w))gtk_container_foreach(GTK_CONTAINER(w),collect,nullptr);
}
static gboolean drive(gpointer){
    assert(++ticks<200);GList *windows=gtk_window_list_toplevels();
    for(auto *at=windows;at;at=at->next){
        auto *w=GTK_WIDGET(at->data);if(!GTK_IS_DIALOG(w)||!gtk_widget_get_visible(w))continue;
        fields.clear();checks.clear();labels.clear();collect(w,nullptr);
        int width=gtk_widget_get_allocated_width(w),height=gtk_widget_get_allocated_height(w);assert(width<=1024&&height<=768);
        if(phase==0&&!fields.empty()){
            if(!removing){assert(checks.size()==1);auto *button=gtk_dialog_get_widget_for_response(GTK_DIALOG(w),GTK_RESPONSE_ACCEPT);assert(!gtk_widget_get_sensitive(button));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[0]),true);}
            assert(fields.size()==(removing?2u:1u));gtk_entry_set_text(GTK_ENTRY(fields[0]),"Test-only-password-491");
            if(removing)gtk_entry_set_text(GTK_ENTRY(fields[1]),saved_recovery.c_str());
            ++phase;gtk_dialog_response(GTK_DIALOG(w),GTK_RESPONSE_ACCEPT);break;
        }
        if(!removing&&phase==1){
            for(auto *label:labels){std::string text=gtk_label_get_text(GTK_LABEL(label));if(text.size()==8*33&&text[32]=='\n')saved_recovery=text.substr(0,32);}
            if(saved_recovery.empty())continue;
            ++phase;gtk_dialog_response(GTK_DIALOG(w),GTK_RESPONSE_ACCEPT);break;
        }
        if((!removing&&phase==2)||(removing&&phase==1)){++phase;gtk_dialog_response(GTK_DIALOG(w),GTK_RESPONSE_CLOSE);break;}
    }
    g_list_free(windows);return phase==(removing?2u:3u)?G_SOURCE_REMOVE:G_SOURCE_CONTINUE;
}
int main(int argc,char **argv){
    assert(getenv("MNOS_AUTH_TEST_CONTAINER")&&getuid()==1000);gtk_init(&argc,&argv);
    g_timeout_add(150,drive,nullptr);assert(configure(LOGIN_ENROLL)&&phase==3);
    removing=true;phase=0;ticks=0;g_timeout_add(150,drive,nullptr);assert(configure(LOGIN_DISABLE)&&phase==2);
    auth_wipe(saved_recovery.data(),saved_recovery.size());
    puts("PASS face settings GTK: explicit consent, protected broker enrollment, recovery confirmation, recovery-assisted removal and 1024x768 layout. Recognition itself is a test fixture.");
}
