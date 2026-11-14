// SPDX-License-Identifier: GPL-3.0-or-later
#include "region-ui.hpp"
#include <gio/gio.h>
#include <iostream>

struct Settings {
    GtkWidget *window{}, *save{}, *status{};
    region::Fields fields;
    explicit Settings(const region::Config &config) : fields(config) {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), mnos_text("Language, Region & Time", "语言、地区与时间"));
        gtk_window_set_default_size(GTK_WINDOW(window), 720, 460);
        auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_container_set_border_width(GTK_CONTAINER(box), 24);
        gtk_container_add(GTK_CONTAINER(window), box);
        gtk_box_pack_start(GTK_BOX(box), fields.grid, false, false, 0);
        status = gtk_label_new(mnos_text("Language and regional formats apply to the next login.", "语言和地区格式将在下次登录时生效。"));
        gtk_label_set_line_wrap(GTK_LABEL(status), true);
        gtk_label_set_max_width_chars(GTK_LABEL(status), 75);
        gtk_box_pack_start(GTK_BOX(box), status, false, false, 0);
        save = gtk_button_new_with_label(mnos_text("Save settings…", "保存设置…"));
        gtk_box_pack_start(GTK_BOX(box), save, false, false, 0);
        g_signal_connect_swapped(save, "clicked", G_CALLBACK(+[](Settings *s) { s->apply(); }), this);
        g_signal_connect(window, "delete-event", G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean { gtk_main_quit(); return false; }), nullptr);
        gtk_widget_show_all(window);
    }
    void apply() {
        try {
            const auto config = fields.value();
            region::validate(config);
            GError *error = nullptr;
            auto *p = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE), &error,
                "/usr/bin/pkexec", "/usr/lib/mnos/configure-region", "--apply", nullptr);
            if (!p) { std::string message = error->message; g_error_free(error); throw std::runtime_error(message); }
            gtk_widget_set_sensitive(fields.grid, false); gtk_widget_set_sensitive(save, false);
            g_subprocess_communicate_utf8_async(p, region::serialize(config).c_str(), nullptr,
                +[](GObject *source, GAsyncResult *result, gpointer data) {
                    auto *s = static_cast<Settings *>(data);
                    gchar *output = nullptr, *errors = nullptr; GError *failure = nullptr;
                    bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &output, &errors, &failure) &&
                        g_subprocess_get_successful(G_SUBPROCESS(source));
                    gtk_label_set_text(GTK_LABEL(s->status), ok ? mnos_text(
                        "Saved. Sign out and back in to apply the language. Time synchronization will use the selected servers.",
                        "已保存。重新登录后应用语言；时间同步将使用所选服务器。") :
                        failure ? failure->message : errors && *errors ? errors : mnos_text("Settings were not saved.", "设置未保存。"));
                    g_clear_error(&failure); g_free(output); g_free(errors);
                    gtk_widget_set_sensitive(s->fields.grid, true); gtk_widget_set_sensitive(s->save, true);
                    g_object_unref(source);
                }, this);
        } catch (const std::exception &e) { gtk_label_set_text(GTK_LABEL(status), e.what()); }
    }
};
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    region::Config config;
    config.language = mnos_chinese() ? "zh_CN.UTF-8" : "en_US.UTF-8";
    try { std::ifstream input("/etc/mnos/region"); if (input) {config = region::parse(input); region::validate(config);} }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; }
    Settings settings(config); gtk_main(); return 0;
}
