// SPDX-License-Identifier: GPL-3.0-or-later
// Installed Linux desktop applications, launched by GIO without a host shell.
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include "mnos-locale.h"
enum Column {Icon,Name,Description,Id,Search,Columns};
static GtkWidget *window,*search_entry,*status_label,*tree;
static GtkListStore *rows;
static GtkTreeModel *filtered;
static std::string search_text;
static std::vector<GAppInfo *> applications;
static std::string fold(const char *text) {
    gchar *lower=g_utf8_casefold(text?text:"",-1);std::string result=lower?lower:"";g_free(lower);return result;
}
static void refresh() {
    for(auto *app:applications)g_object_unref(app);
    applications.clear();
    GList *all=g_app_info_get_all();
    for(auto *item=all;item;item=item->next) {
        auto *app=G_APP_INFO(item->data);
        if(g_app_info_should_show(app)&&g_app_info_get_id(app))applications.push_back(G_APP_INFO(g_object_ref(app)));
    }
    g_list_free_full(all,g_object_unref);
    std::sort(applications.begin(),applications.end(),[](auto *a,auto *b){return g_utf8_collate(g_app_info_get_display_name(a),g_app_info_get_display_name(b))<0;});
    if(!rows)return;
    gtk_list_store_clear(rows);
    for(auto *app:applications) {
        const auto *name=g_app_info_get_display_name(app),*id=g_app_info_get_id(app),*description=g_app_info_get_description(app);
        const auto key=fold(name)+" "+fold(id)+" "+fold(description);
        GtkTreeIter row;gtk_list_store_append(rows,&row);
        gtk_list_store_set(rows,&row,Icon,g_app_info_get_icon(app),Name,name,Description,description?description:"",Id,id,Search,key.c_str(),-1);
    }
    const auto count=std::to_string(applications.size())+mnos_text(" installed applications"," 个已安装应用");
    gtk_label_set_text(GTK_LABEL(status_label),count.c_str());
}
static void show() {gtk_widget_show_all(window);gtk_window_present(GTK_WINDOW(window));}
static void message(const std::string &text) {gtk_label_set_text(GTK_LABEL(status_label),text.c_str());}
static void watch_child(GPid pid,const char *name) {
    auto *label=new std::string(name?name:"Application");
    g_child_watch_add(pid,[](GPid child,gint status,gpointer data){
        auto *label=static_cast<std::string *>(data);GError *error=nullptr;
        if(!g_spawn_check_wait_status(status,&error)) {
            const std::string detail=*label+": "+(error?error->message:"Application failed.");
            g_printerr("%s\n",detail.c_str());message(detail);show();g_clear_error(&error);
        }
        g_spawn_close_pid(child);delete label;
    },label);
}
static bool open_app(const std::string &id,std::string &error) {
    if(id.empty()||id.size()>255||id.find_first_of("/\\\r\n")!=std::string::npos){error="Invalid desktop ID.";return false;}
    // Refresh also supports a CLI request immediately after apt installs a new
    // application, before the file-monitor notification reaches the GTK loop.
    refresh();
    for(auto *app:applications)if(id==g_app_info_get_id(app)) {
        auto *context=gdk_display_get_app_launch_context(gdk_display_get_default());GError *failure=nullptr;
        const bool ok=G_IS_DESKTOP_APP_INFO(app)?g_desktop_app_info_launch_uris_as_manager(
            G_DESKTOP_APP_INFO(app),nullptr,G_APP_LAUNCH_CONTEXT(context),
            GSpawnFlags(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD),nullptr,nullptr,
            [](GDesktopAppInfo *info,GPid pid,gpointer){watch_child(pid,g_app_info_get_display_name(G_APP_INFO(info)));},nullptr,&failure):
            g_app_info_launch(app,nullptr,G_APP_LAUNCH_CONTEXT(context),&failure);
        g_object_unref(context);
        if(!ok){error=failure?failure->message:"Application could not be started.";g_clear_error(&failure);return false;}
        message(std::string(mnos_text("Started: ","已启动："))+g_app_info_get_display_name(app));return true;
    }
    error="Application is not installed or has no visible desktop entry: "+id;return false;
}
static bool spawn(char **args,std::string &error) {
    if(!args||!args[0]||!*args[0]){error="An executable is required.";return false;}
    GError *failure=nullptr;GPid pid=0;
    const bool ok=g_spawn_async(nullptr,args,nullptr,GSpawnFlags(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD),nullptr,nullptr,&pid,&failure);
    if(ok)watch_child(pid,args[0]);
    if(!ok){error=failure?failure->message:"Cannot start application.";g_clear_error(&failure);}return ok;
}
static std::string dispatch(const std::string &command) {
    if(command=="show"){show();return "OK\nMNOS_GUI_READY\n";}
    if(command=="hide"){gtk_widget_hide(window);return "OK\nMNOS_GUI_READY\n";}
    if(command=="status")return "OK\nMNOS_GUI_READY\n";
    if(command=="list") {
        refresh();std::string reply="OK\n";
        for(auto *app:applications)reply+=std::string(g_app_info_get_id(app))+"\t"+g_app_info_get_display_name(app)+"\n";
        return reply;
    }
    std::string error;
    if(command.compare(0,5,"open ")==0) {
        if(open_app(command.substr(5),error))return "OK\nApplication launch requested.\n";
    } else if(command.compare(0,4,"run ")==0) {
        gchar **args=nullptr;GError *failure=nullptr;
        // This parser only splits/quotes argv. It never expands variables,
        // substitutions or shell operators and does not execute /bin/sh.
        if(g_shell_parse_argv(command.c_str()+4,nullptr,&args,&failure)) {
            const bool ok=spawn(args,error);g_strfreev(args);if(ok)return "OK\nApplication launch requested.\n";
        } else {error=failure?failure->message:"Invalid application arguments.";g_clear_error(&failure);}
    } else error="Unknown desktop request.";
    return "ERROR "+error+"\n";
}
struct Request {
    GSocketConnection *connection;
    GCancellable *cancel=g_cancellable_new();
    std::array<char,512> input{};
    size_t used=0;
    guint timeout=0;
    std::string output;
    explicit Request(GSocketConnection *c):connection(G_SOCKET_CONNECTION(g_object_ref(c))){}
    ~Request(){if(timeout)g_source_remove(timeout);g_io_stream_close(G_IO_STREAM(connection),nullptr,nullptr);g_object_unref(connection);g_object_unref(cancel);}
};
static void write_done(GObject *object,GAsyncResult *result,gpointer data) {
    auto *r=static_cast<Request *>(data);GError *error=nullptr;
    g_output_stream_write_all_finish(G_OUTPUT_STREAM(object),result,nullptr,&error);g_clear_error(&error);delete r;
}
static void respond(Request *r,std::string output) {
    r->output=std::move(output);
    g_output_stream_write_all_async(g_io_stream_get_output_stream(G_IO_STREAM(r->connection)),r->output.data(),r->output.size(),G_PRIORITY_DEFAULT,r->cancel,write_done,r);
}
static void read_request(Request *r);
static void read_done(GObject *object,GAsyncResult *result,gpointer data) {
    auto *r=static_cast<Request *>(data);GError *error=nullptr;
    const auto n=g_input_stream_read_finish(G_INPUT_STREAM(object),result,&error);
    if(n<=0){g_clear_error(&error);delete r;return;}
    r->used+=size_t(n);
    const auto *newline=static_cast<const char *>(memchr(r->input.data(),'\n',r->used));
    if(newline) {
        const size_t size=size_t(newline-r->input.data());
        if(memchr(r->input.data(),0,size)||!g_utf8_validate(r->input.data(),gssize(size),nullptr))respond(r,"ERROR Invalid request encoding.\n");
        else respond(r,dispatch(std::string(r->input.data(),size)));
    } else if(r->used==r->input.size())respond(r,"ERROR Request is too long.\n");
    else read_request(r);
}
static void read_request(Request *r) {
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(r->connection)),r->input.data()+r->used,r->input.size()-r->used,G_PRIORITY_DEFAULT,r->cancel,read_done,r);
}
static gboolean incoming(GSocketService *,GSocketConnection *connection,GObject *,gpointer) {
    GError *error=nullptr;GCredentials *credentials=g_socket_get_credentials(g_socket_connection_get_socket(connection),&error);
    const bool allowed=credentials&&g_credentials_get_unix_user(credentials,&error)==getuid();
    if(credentials)g_object_unref(credentials);
    g_clear_error(&error);
    if(!allowed){g_io_stream_close(G_IO_STREAM(connection),nullptr,nullptr);return TRUE;}
    auto *r=new Request(connection);
    r->timeout=g_timeout_add_seconds(5,[](gpointer data)->gboolean{auto *r=static_cast<Request *>(data);r->timeout=0;g_cancellable_cancel(r->cancel);return G_SOURCE_REMOVE;},r);
    read_request(r);return TRUE;
}
static void activate_row(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *,gpointer) {
    GtkTreeIter row;auto *model=gtk_tree_view_get_model(view);
    if(!gtk_tree_model_get_iter(model,&row,path))return;
    gchar *id=nullptr;gtk_tree_model_get(model,&row,Id,&id,-1);std::string error;
    if(!open_app(id?id:"",error))message(error);
    g_free(id);
}
static void install_package(GtkButton *,gpointer) {
    auto *dialog=gtk_dialog_new_with_buttons(mnos_text("Install a Linux package","安装 Linux 软件包"),GTK_WINDOW(window),GTK_DIALOG_MODAL,
        mnos_text("Cancel","取消"),GTK_RESPONSE_CANCEL,mnos_text("Open APT terminal","打开 APT 终端"),GTK_RESPONSE_ACCEPT,nullptr);
    auto *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),18);
    auto *entry=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"firefox-esr");gtk_entry_set_max_length(GTK_ENTRY(entry),255);
    gtk_box_pack_start(GTK_BOX(box),gtk_label_new(mnos_text("Package name (use packages for this Linux architecture)","软件包名称（使用匹配当前 Linux 架构的软件包）")),FALSE,FALSE,6);
    gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,6);gtk_widget_show_all(dialog);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
        std::string name=gtk_entry_get_text(GTK_ENTRY(entry));bool valid=!name.empty()&&((name[0]>='a'&&name[0]<='z')||(name[0]>='0'&&name[0]<='9'));
        for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'||c==':'))valid=false;
        if(valid) {
            char *args[]={const_cast<char *>("xterm"),const_cast<char *>("-hold"),const_cast<char *>("-T"),const_cast<char *>("mnOS packages"),
                const_cast<char *>("-e"),const_cast<char *>("/usr/local/bin/mnos-pkg"),const_cast<char *>("install"),name.data(),nullptr};
            std::string error;if(!spawn(args,error))message(error);
        } else message(mnos_text("Enter one package name; options and paths are not accepted.","请输入一个软件包名，不接受选项或路径。"));
    }
    gtk_widget_destroy(dialog);
}
int main(int argc,char **argv) {
    if(argc!=2||strcmp(argv[1],"--serve")){g_printerr("Usage: mnos-apps --serve (started by the Linux desktop session)\n");return 2;}
    if(!gtk_init_check(&argc,&argv)){g_printerr("No X11 display. Use mnos-gui show.\n");return 1;}
    const char *setting=getenv("MNOS_GUI_DIRECTORY");const std::string directory=setting?setting:"/run/mnos-gui";
    if(mkdir(directory.c_str(),0700)&&errno!=EEXIST){perror("desktop directory");return 1;}
    struct stat st{};
    if(lstat(directory.c_str(),&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0077))return 1;
    const int lock=open((directory+"/service.lock").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;
    const auto socket=directory+"/socket";
    if(!lstat(socket.c_str(),&st)) {if(!S_ISSOCK(st.st_mode)||st.st_uid!=getuid())return 1;unlink(socket.c_str());}
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),mnos_text("mnOS Linux applications","mnOS Linux 应用"));
    gtk_window_set_default_size(GTK_WINDOW(window),820,500);
    g_object_set(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",TRUE,nullptr);
    auto *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_container_set_border_width(GTK_CONTAINER(box),18);gtk_container_add(GTK_CONTAINER(window),box);
    auto *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
    search_entry=gtk_search_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry),mnos_text("Search installed applications","搜索已安装的应用"));gtk_box_pack_start(GTK_BOX(bar),search_entry,TRUE,TRUE,0);
    auto *install=gtk_button_new_with_label(mnos_text("Install package","安装软件包"));gtk_box_pack_start(GTK_BOX(bar),install,FALSE,FALSE,0);
    auto *reload=gtk_button_new_with_label(mnos_text("Refresh","刷新"));gtk_box_pack_start(GTK_BOX(bar),reload,FALSE,FALSE,0);
    auto *scroll=gtk_scrolled_window_new(nullptr,nullptr);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
    rows=gtk_list_store_new(Columns,G_TYPE_ICON,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(rows),nullptr);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered),[](GtkTreeModel *model,GtkTreeIter *row,gpointer)->gboolean {
        gchar *key=nullptr;gtk_tree_model_get(model,row,Search,&key,-1);const bool visible=search_text.empty()||(key&&std::string(key).find(search_text)!=std::string::npos);g_free(key);return visible;
    },nullptr,nullptr);
    tree=gtk_tree_view_new_with_model(filtered);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree),FALSE);
    auto *icon=gtk_cell_renderer_pixbuf_new();g_object_set(icon,"stock-size",GTK_ICON_SIZE_LARGE_TOOLBAR,"ypad",8,"xpad",8,nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),gtk_tree_view_column_new_with_attributes("",icon,"gicon",Icon,nullptr));
    auto *name=gtk_cell_renderer_text_new();g_object_set(name,"weight",600,"ypad",8,nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),gtk_tree_view_column_new_with_attributes("",name,"text",Name,nullptr));
    auto *description=gtk_cell_renderer_text_new();g_object_set(description,"ellipsize",PANGO_ELLIPSIZE_END,"xpad",18,nullptr);
    auto *details=gtk_tree_view_column_new_with_attributes("",description,"text",Description,nullptr);gtk_tree_view_column_set_expand(details,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(tree),details);
    status_label=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(status_label),0);gtk_label_set_line_wrap(GTK_LABEL(status_label),TRUE);gtk_box_pack_start(GTK_BOX(box),status_label,FALSE,FALSE,0);
    g_signal_connect(window,"delete-event",G_CALLBACK(+[](GtkWidget *widget,GdkEvent *,gpointer)->gboolean{gtk_widget_hide(widget);return TRUE;}),nullptr);
    g_signal_connect(search_entry,"search-changed",G_CALLBACK(+[](GtkEntry *entry,gpointer){search_text=fold(gtk_entry_get_text(entry));gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filtered));}),nullptr);
    g_signal_connect(tree,"row-activated",G_CALLBACK(activate_row),nullptr);
    g_signal_connect(install,"clicked",G_CALLBACK(install_package),nullptr);
    g_signal_connect(reload,"clicked",G_CALLBACK(+[](GtkButton *,gpointer){refresh();}),nullptr);
    auto *monitor=g_app_info_monitor_get();g_signal_connect(monitor,"changed",G_CALLBACK(+[](GAppInfoMonitor *,gpointer){refresh();}),nullptr);
    refresh();
    auto *service=g_socket_service_new();auto *address=g_unix_socket_address_new(socket.c_str());GError *error=nullptr;
    if(!g_socket_listener_add_address(G_SOCKET_LISTENER(service),address,G_SOCKET_TYPE_STREAM,G_SOCKET_PROTOCOL_DEFAULT,nullptr,nullptr,&error)) {
        g_printerr("Cannot start application service: %s\n",error?error->message:"unknown error");g_clear_error(&error);return 1;
    }
    chmod(socket.c_str(),0600);g_object_unref(address);g_signal_connect(service,"incoming",G_CALLBACK(incoming),nullptr);g_socket_service_start(service);
    g_print("MNOS_GUI_SERVICE_READY\n");gtk_main();
    g_socket_service_stop(service);g_object_unref(service);g_object_unref(monitor);unlink(socket.c_str());close(lock);
    for(auto *app:applications)g_object_unref(app);
    g_object_unref(filtered);g_object_unref(rows);return 0;
}
