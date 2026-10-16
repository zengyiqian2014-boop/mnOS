// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <csignal>
#include <iostream>
#include <sys/resource.h>

static void marker(const std::string &text) {
    std::cout << text << std::endl;
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NOCTTY);
    if (fd >= 0) {
        std::string line = "<6>" + text + '\n';
        ssize_t written = write(fd, line.data(), line.size());
        (void)written;
        close(fd);
    }
}

struct Installer {
    GtkWidget *window{}, *form{}, *disks{}, *name{}, *password{}, *again{}, *network{},
        *status{}, *progress{}, *log{}, *install{}, *refresh{}, *finish{};
    GSubprocess *child{};
    GDataInputStream *output{};
    bool running = false, complete = false;
    std::string device;

    void message(const std::string &text, GtkMessageType type = GTK_MESSAGE_ERROR) {
        auto *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, type,
                                               GTK_BUTTONS_CLOSE, "%s", text.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    void append(const std::string &line) {
        auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, (line + '\n').c_str(), -1);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log), &end, 0, false, 0, 1);
    }
    static GtkWidget *label(const char *text, const char *style = nullptr) {
        auto *w = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(w), 0);
        gtk_label_set_line_wrap(GTK_LABEL(w), true);
        if (style) gtk_style_context_add_class(gtk_widget_get_style_context(w), style);
        return w;
    }
    void reload() {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(disks));
        GError *error = nullptr;
        auto *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error,
                                  "/usr/lib/mnos/install-system", "--list", nullptr);
        gchar *data = nullptr;
        bool ok = p && g_subprocess_communicate_utf8(p, nullptr, nullptr, &data, nullptr, &error)
                    && g_subprocess_get_successful(p);
        unsigned count = 0;
        auto *parser = json_parser_new();
        if (ok && data && json_parser_load_from_data(parser, data, -1, &error)) {
            auto *root = json_parser_get_root(parser);
            if (JSON_NODE_HOLDS_ARRAY(root)) {
                auto *array = json_node_get_array(root);
                for (unsigned i = 0; i < json_array_get_length(array); ++i) {
                    auto *disk = json_array_get_object_element(array, i);
                    const char *path = json_object_get_string_member(disk, "path");
                    auto *model_node = json_object_get_member(disk, "model");
                    const char *model = JSON_NODE_HOLDS_NULL(model_node) ? "Disk" : json_node_get_string(model_node);
                    double gib = double(json_object_get_int_member(disk, "size")) / 1073741824.0;
                    gchar *title = g_strdup_printf("%s   ·   %.1f GiB   ·   %s", path, gib, model);
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(disks), path, title);
                    g_free(title);
                    ++count;
                }
            }
        }
        if (error) { append(error->message); g_error_free(error); }
        g_free(data);
        g_object_unref(parser);
        if (p) g_object_unref(p);
        gtk_combo_box_set_active(GTK_COMBO_BOX(disks), count ? 0 : -1);
        gtk_widget_set_sensitive(install, count != 0);
        gtk_label_set_text(GTK_LABEL(status), count ?
            "安装前会再次确认目标磁盘，确认后才开始写入。" :
            "未找到可安装磁盘。请添加至少 32 GiB 的可写磁盘，再点击刷新。已挂载的磁盘不会列出。");
    }
    static void read_line(GObject *source, GAsyncResult *result, gpointer data) {
        auto *self = static_cast<Installer *>(data);
        GError *error = nullptr;
        gchar *line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(source), result, nullptr, &error);
        if (line) {
            self->append(line);
            if (g_str_has_prefix(line, "STAGE ")) {
                char *end = nullptr;
                long percent = strtol(line + 6, &end, 10);
                if (end && percent >= 0 && percent <= 100) {
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress), double(percent) / 100.0);
                    gtk_label_set_text(GTK_LABEL(self->status), *end == ' ' ? end + 1 : end);
                }
            }
            // Progress is also available through the VM serial console; no secrets are logged.
            if (g_str_has_prefix(line, "STAGE ") || g_str_has_prefix(line, "MNOS "))
                marker(line);
            g_free(line);
            g_data_input_stream_read_line_async(self->output, G_PRIORITY_DEFAULT, nullptr, read_line, self);
        } else if (error) { self->append(error->message); g_error_free(error); }
    }
    static void exited(GObject *source, GAsyncResult *result, gpointer data) {
        auto *self = static_cast<Installer *>(data);
        GError *error = nullptr;
        bool ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);
        self->running = false;
        self->complete = ok;
        if (ok) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress), 1);
            gtk_label_set_text(GTK_LABEL(self->status), "安装完成。先关机，移除 ISO / 安装 U 盘，再从硬盘启动。");
            gtk_widget_show(self->finish);
        } else {
            self->message("安装未完成。请查看下方日志；目标盘可能已被重新分区。\n" +
                          std::string(error ? error->message : "Installation failed"));
            gtk_label_set_text(GTK_LABEL(self->status), "安装失败。日志保留在当前窗口及系统日志中。");
            gtk_widget_set_sensitive(self->form, true);
        }
        if (error) g_error_free(error);
    }
    void begin() {
        const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(disks));
        if (!selected) { message("请选择目标磁盘。"); return; }
        device = selected;
        std::string account = gtk_entry_get_text(GTK_ENTRY(name));
        std::string secret = gtk_entry_get_text(GTK_ENTRY(password));
        std::string repeat = gtk_entry_get_text(GTK_ENTRY(again));
        if (!mnos::username(account) || secret.size() < 12 || secret.size() > 512 || secret != repeat) {
            mnos::erase(secret); mnos::erase(repeat);
            message("用户名以小写字母开头，最多 31 个字母、数字、下划线或连字符。\n密码需为 12–512 字符，且两次输入一致。");
            return;
        }
        mnos::erase(repeat);
        std::string quality_input = secret + '\n';
        int quality = mnos::run({"/usr/bin/pwscore", account}, quality_input);
        mnos::erase(quality_input);
        if (quality) { mnos::erase(secret); message("密码过于简单。请使用较长、难猜的独立密码。"); return; }
        auto *dialog = gtk_dialog_new_with_buttons("确认清空目标磁盘", GTK_WINDOW(window), GTK_DIALOG_MODAL,
                                                  "取消", GTK_RESPONSE_CANCEL, "清空并安装", GTK_RESPONSE_ACCEPT, nullptr);
        auto *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 24);
        std::string warning = "将删除 " + device + " 上的全部分区和文件。\n此操作无法撤销。请输入完整磁盘路径确认：";
        gtk_box_pack_start(GTK_BOX(content), label(warning.c_str()), false, false, 8);
        auto *confirmation = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), device.c_str());
        gtk_box_pack_start(GTK_BOX(content), confirmation, false, false, 8);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        gtk_entry_set_activates_default(GTK_ENTRY(confirmation), true);
        gtk_widget_show_all(dialog);
        gtk_widget_grab_focus(confirmation);
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        bool confirmed = response == GTK_RESPONSE_ACCEPT && device == gtk_entry_get_text(GTK_ENTRY(confirmation));
        gtk_widget_destroy(dialog);
        if (!confirmed) { mnos::erase(secret); return; }
        GError *error = nullptr;
        child = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE |
            G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE), &error,
            "/usr/lib/mnos/install-system", "--install", device.c_str(), "--confirm", device.c_str(), nullptr);
        std::string input = account + '\n' + secret + '\n' +
            (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(network)) ? "delegated\n" : "root\n");
        mnos::erase(secret);
        bool sent = child && g_output_stream_write_all(g_subprocess_get_stdin_pipe(child), input.data(), input.size(), nullptr, nullptr, &error);
        mnos::erase(input);
        gtk_entry_set_text(GTK_ENTRY(password), "");
        gtk_entry_set_text(GTK_ENTRY(again), "");
        if (child) g_output_stream_close(g_subprocess_get_stdin_pipe(child), nullptr, nullptr);
        if (!sent) {
            message(error ? error->message : "Cannot start installer");
            if (error) g_error_free(error);
            if (child) { g_subprocess_force_exit(child); g_object_unref(child); child = nullptr; }
            return;
        }
        running = true;
        gtk_widget_set_sensitive(form, false);
        output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(child));
        g_data_input_stream_read_line_async(output, G_PRIORITY_DEFAULT, nullptr, read_line, this);
        g_subprocess_wait_check_async(child, nullptr, exited, this);
    }
    Installer() {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "mnOS 安装程序 / Installer");
        gtk_window_set_default_size(GTK_WINDOW(window), 1000, 720);
        gtk_window_fullscreen(GTK_WINDOW(window));
        auto *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css,
            "window{background:#101923;color:#eaf0f6;} label{font-family:Sans;font-size:14px;}"
            ".brand{font-size:18px;font-weight:700;color:#8bdad1;letter-spacing:2px;}"
            ".title{font-size:30px;font-weight:700;} .muted{color:#a6b7c7;font-size:13px;}"
            "entry,combobox button{min-height:30px;} button{padding:8px 18px;}"
            ".suggested-action{background:#66c7ba;color:#102327;font-weight:700;}"
            "progressbar progress{background:#66c7ba;} textview text{background:#0b121a;color:#c8d6e2;}", -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        auto *scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(window), scroll);
        auto *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(body), 28);
        gtk_widget_set_halign(body, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(body, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(body, 720, -1);
        gtk_container_add(GTK_CONTAINER(scroll), body);
        gtk_box_pack_start(GTK_BOX(body), label("mnOS  /  INSTALL", "brand"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(body), label("从这里，开始你的系统。", "title"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(body), label("离线安装 · 桌面与开发工具 · 虚拟机和实体硬件", "muted"), false, false, 0);
        form = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(form), 10);
        gtk_grid_set_column_spacing(GTK_GRID(form), 18);
        gtk_box_pack_start(GTK_BOX(body), form, false, false, 8);
        auto field = [&](const char *caption, GtkWidget *widget, int row) {
            auto *caption_widget = gtk_label_new_with_mnemonic(caption);
            gtk_label_set_xalign(GTK_LABEL(caption_widget), 0);
            gtk_label_set_mnemonic_widget(GTK_LABEL(caption_widget), widget);
            gtk_grid_attach(GTK_GRID(form), caption_widget, 0, row, 1, 1);
            gtk_widget_set_hexpand(widget, true);
            gtk_grid_attach(GTK_GRID(form), widget, 1, row, 1, 1);
        };
        disks = gtk_combo_box_text_new(); field("目标磁盘 (_D)", disks, 0);
        refresh = gtk_button_new_with_label("刷新");
        gtk_grid_attach(GTK_GRID(form), refresh, 2, 0, 1, 1);
        name = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(name), "你的用户名，例如 alex"); field("用户名 (_U)", name, 1);
        password = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(password), false); field("密码 (_P)", password, 2);
        again = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(again), false); field("确认密码 (_C)", again, 3);
        network = gtk_check_button_new_with_label("向此账号开放 NET_ADMIN / NET_RAW 网络工具权限");
        gtk_grid_attach(GTK_GRID(form), network, 1, 4, 2, 1);
        gtk_grid_attach(GTK_GRID(form), label("默认关闭：特权网络操作需 sudo；日常应用无需 root。", "muted"), 1, 5, 2, 1);
        install = gtk_button_new_with_mnemonic("开始安装 (_I)");
        gtk_style_context_add_class(gtk_widget_get_style_context(install), "suggested-action");
        gtk_grid_attach(GTK_GRID(form), install, 1, 6, 1, 1);
        status = label("正在发现磁盘…", "muted");
        gtk_box_pack_start(GTK_BOX(body), status, false, false, 0);
        progress = gtk_progress_bar_new(); gtk_box_pack_start(GTK_BOX(body), progress, false, false, 0);
        auto *expander = gtk_expander_new("安装日志 / Installation log");
        log = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(log), false);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(log), true);
        auto *logs = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(logs, -1, 150); gtk_container_add(GTK_CONTAINER(logs), log);
        gtk_container_add(GTK_CONTAINER(expander), logs); gtk_box_pack_start(GTK_BOX(body), expander, true, true, 0);
        finish = gtk_button_new_with_mnemonic("关机，移除安装介质 (_S)");
        gtk_box_pack_start(GTK_BOX(body), finish, false, false, 0);
        g_signal_connect_swapped(refresh, "clicked", G_CALLBACK(+[](Installer *s) { s->reload(); }), this);
        g_signal_connect_swapped(install, "clicked", G_CALLBACK(+[](Installer *s) { s->begin(); }), this);
        g_signal_connect_swapped(finish, "clicked", G_CALLBACK(+[](Installer *s) {
            if (s->complete) mnos::run({"/usr/bin/systemctl", "poweroff"});
        }), this);
        g_signal_connect(window, "delete-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
            auto *s = static_cast<Installer *>(data);
            s->message(s->running ? "正在安装，请勿关闭电源。" : "如需退出，请关闭虚拟机或计算机。", GTK_MESSAGE_INFO);
            return true;
        }), this);
        gtk_widget_show_all(window); gtk_widget_hide(finish);
        reload(); gtk_widget_grab_focus(name);
        g_idle_add(+[](gpointer) -> gboolean { marker("MNOS INSTALLER GUI READY"); return G_SOURCE_REMOVE; }, nullptr);
    }
};
int main(int argc, char **argv) {
    if (geteuid() != 0) { std::cerr << "Run the installer from its boot entry.\n"; return 1; }
    const rlimit no_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core)) return 1;
    signal(SIGPIPE, SIG_IGN);
    gtk_init(&argc, &argv);
    Installer installer;
    gtk_main();
}
