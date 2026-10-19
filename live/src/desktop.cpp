// SPDX-License-Identifier: GPL-3.0-or-later
// Original mnOS desktop tools. GTK/GIO provide rendering and XDG integration;
// layout, filtering, catalog, launch policy and metrics are implemented here.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <iostream>
#include <json-glib/json-glib.h>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>
namespace fs = std::filesystem;
struct Entry {
    std::string name, description, category, icon, id, search;
    std::vector<std::string> command;
    GAppInfo *app = nullptr;
};
static std::vector<Entry> entries;
static GtkWidget *window, *search, *list, *summary;
static bool popup = false, testing = false;
static std::string selected = "常用";
static const char *groups[] = {"常用",     "全部应用",   "系统设置",       "开发编程",
                               "网络工具", "逆向与调试", "Web 与密码工具", "取证与文件",
                               "系统工具", "其他应用"};
static const char *group_icons[] = {"starred",
                                    "view-app-grid",
                                    "preferences-system",
                                    "applications-development",
                                    "network-workgroup",
                                    "applications-debugging",
                                    "security-high",
                                    "folder-saved-search",
                                    "utilities-system-monitor",
                                    "applications-other"};
static std::string folded(const std::string &s) {
    char *p = g_utf8_casefold(s.c_str(), -1);
    std::string r = p ? p : "";
    g_free(p);
    return r;
}
static std::string read(const char *path) {
    std::ifstream f(path);
    std::ostringstream b;
    b << f.rdbuf();
    return b.str();
}
static std::string id_for(const std::string &s) {
    char *v = g_compute_checksum_for_string(G_CHECKSUM_SHA256, s.c_str(), -1);
    std::string id(v, 12);
    g_free(v);
    return id;
}
static const char *string_member(JsonObject *o, const char *key) {
    if (!json_object_has_member(o, key))
        throw std::runtime_error(std::string("Missing catalog field: ") + key);
    JsonNode *n = json_object_get_member(o, key);
    if (!JSON_NODE_HOLDS_VALUE(n) || json_node_get_value_type(n) != G_TYPE_STRING)
        throw std::runtime_error("Invalid catalog string");
    return json_node_get_string(n);
}
static void catalog(const char *path, bool optional = false) {
    JsonParser *parser = json_parser_new();
    GError *error = nullptr;
    if (!json_parser_load_from_file(parser, path, &error)) {
        std::string message = error ? error->message : "Cannot read catalog";
        g_clear_error(&error);
        g_object_unref(parser);
        if (optional)
            return;
        throw std::runtime_error(message);
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root)) {
        g_object_unref(parser);
        throw std::runtime_error("Catalog must be an array");
    }
    JsonArray *array = json_node_get_array(root);
    for (guint i = 0; i < json_array_get_length(array); ++i) {
        JsonObject *o = json_array_get_object_element(array, i);
        if (!o)
            throw std::runtime_error("Invalid catalog entry");
        Entry e;
        e.name = string_member(o, "name");
        e.description = string_member(o, "description");
        e.icon = string_member(o, "icon");
        e.category = string_member(o, "category");
        e.id = id_for(e.name);
        JsonArray *args = json_object_get_array_member(o, "command");
        if (!args || !json_array_get_length(args))
            throw std::runtime_error("Empty tool command");
        for (guint a = 0; a < json_array_get_length(args); ++a) {
            const char *v = json_array_get_string_element(args, a);
            if (!v)
                throw std::runtime_error("Invalid tool argument");
            e.command.emplace_back(v);
        }
        // CLI catalog always opens help in a terminal. GUI actions declare their own mode.
        if (!optional)
            e.command.insert(e.command.begin(), "@terminal");
        e.search = folded(e.name + " " + e.description + " " + e.category);
        for (const auto &arg : e.command)
            e.search += " " + folded(arg);
        entries.push_back(std::move(e));
    }
    g_object_unref(parser);
}
static std::string executable(const std::string &name) {
    if (name.find('/') != std::string::npos)
        return name[0] == '/' && access(name.c_str(), X_OK) == 0 ? name : "";
    for (const char *dir : {"/usr/local/bin/", "/usr/bin/", "/bin/", "/usr/sbin/", "/sbin/"}) {
        std::string p = std::string(dir) + name;
        if (access(p.c_str(), X_OK) == 0)
            return p;
    }
    return "";
}
static void discover() {
    GList *apps = g_app_info_get_all();
    for (GList *node = apps; node; node = node->next) {
        GAppInfo *app = G_APP_INFO(node->data);
        const char *id = g_app_info_get_id(app);
        if (!g_app_info_should_show(app) || !G_IS_DESKTOP_APP_INFO(app) || !id)
            continue;
        if (g_str_has_prefix(id, "mnos-tool-") || std::string(id) == "mnos-tools.desktop")
            continue;
        Entry e;
        e.app = G_APP_INFO(g_object_ref(app));
        e.id = id;
        e.name = g_app_info_get_display_name(app);
        const char *desc = g_app_info_get_description(app);
        e.description = desc ? desc : "";
        const char *raw = g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(app));
        std::string c = raw ? raw : "";
        std::string filename = id;
        e.category = "其他应用";
        if (c.find("Settings;") != std::string::npos || filename == "gparted.desktop" ||
            filename == "org.gnome.DiskUtility.desktop" || filename == "mnos-admin.desktop" ||
            filename == "mnos-center.desktop")
            e.category = "系统设置";
        else if (c.find("Development;") != std::string::npos)
            e.category = "开发编程";
        else if (c.find("Network;") != std::string::npos)
            e.category = "网络工具";
        else if (c.find("System;") != std::string::npos)
            e.category = "系统工具";
        e.search = folded(e.name + " " + e.description + " " + e.category + " " + id);
        entries.push_back(std::move(e));
    }
    g_list_free_full(apps, g_object_unref);
}
static std::string quote_args(const std::vector<std::string> &args) {
    std::string s;
    for (const auto &a : args) {
        char *q = g_shell_quote(a.c_str());
        if (!s.empty())
            s += ' ';
        s += q;
        g_free(q);
    }
    return s;
}
static std::vector<std::string> launch_args(const Entry &e) {
    auto args = e.command;
    if (args.empty())
        return {};
    if (args[0] == "@network") {
        bool delegated = read("/etc/mnos/network-policy") == "netadmin\n" &&
                         access("/usr/lib/mnos/net-admin", X_OK) == 0;
        args[0] = delegated ? "mnos-net" : "sudo";
        args.insert(args.begin(), "@terminal");
    }
    if (args[0] == "@terminal") {
        args.erase(args.begin());
        std::string path = executable(args[0]);
        if (path.empty())
            throw std::runtime_error("未找到程序：" + args[0]);
        args[0] = path;
        // xfce4-terminal parses argv with GLib; it does not invoke a shell.
        return {"/usr/bin/xfce4-terminal", "--hold", "--command", quote_args(args)};
    }
    auto path = executable(args[0]);
    if (path.empty())
        throw std::runtime_error("未找到程序：" + args[0]);
    args[0] = path;
    return args;
}
static void launch(Entry &e) {
    GError *error = nullptr;
    bool ok = false;
    try {
        if (e.app)
            ok = g_app_info_launch(e.app, nullptr, nullptr, &error);
        else {
            auto args = launch_args(e);
            std::vector<char *> argv;
            for (auto &a : args)
                argv.push_back(a.data());
            argv.push_back(nullptr);
            ok = g_spawn_async(nullptr, argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
                               nullptr, &error);
        }
    } catch (const std::exception &ex) {
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED, ex.what());
    }
    if (!ok) {
        bool old_popup = popup;
        popup = false;
        auto *d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE, "无法打开工具");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s",
                                                 error ? error->message : "Unknown error");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        popup = old_popup;
        g_clear_error(&error);
    } else if (popup)
        gtk_widget_destroy(window);
}
static GtkWidget *label(const std::string &s) {
    auto *w = gtk_label_new(s.c_str());
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    return w;
}
static GtkWidget *box(GtkOrientation orientation, int spacing) {
    return gtk_box_new(orientation, spacing);
}
static void margin(GtkWidget *w, int size) {
    gtk_widget_set_margin_start(w, size);
    gtk_widget_set_margin_end(w, size);
    gtk_widget_set_margin_top(w, size);
    gtk_widget_set_margin_bottom(w, size);
}
static void css(GtkWidget *w, const char *name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), name);
}
static bool favorite(const Entry &e) {
    return e.app && (e.id == "code.desktop" || e.id == "com.microsoft.VSCode.desktop" ||
                     e.id == "firefox-esr.desktop" || e.id == "thunar.desktop" ||
                     e.id == "org.xfce.Thunar.desktop" || e.id == "xfce4-terminal.desktop" ||
                     e.id == "mnos-center.desktop" || e.id == "org.xfce.mousepad.desktop");
}
static gboolean filter(GtkListBoxRow *row, gpointer) {
    auto *e = static_cast<Entry *>(g_object_get_data(G_OBJECT(row), "entry"));
    std::string query = folded(gtk_entry_get_text(GTK_ENTRY(search)));
    if (!query.empty())
        return e->search.find(query) != std::string::npos;
    return selected == "全部应用" || (selected == "常用" ? favorite(*e) : selected == e->category);
}
static void changed(GtkSearchEntry *, gpointer) {
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(list));
}
static void activate_first(GtkEntry *, gpointer) {
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(list));
    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (auto *n = rows; n; n = n->next)
        if (gtk_widget_get_child_visible(GTK_WIDGET(n->data))) {
            launch(*static_cast<Entry *>(g_object_get_data(G_OBJECT(n->data), "entry")));
            break;
        }
    g_list_free(rows);
}
static gboolean key(GtkWidget *, GdkEventKey *event, gpointer) {
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(window);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down && gtk_widget_has_focus(search)) {
        gtk_widget_child_focus(list, GTK_DIR_TAB_FORWARD);
        return TRUE;
    }
    return FALSE;
}
static gboolean lost_focus(GtkWidget *, GdkEventFocus *, gpointer) {
    if (popup && !testing)
        gtk_widget_destroy(window);
    return FALSE;
}
static gboolean metrics(gpointer) {
    static unsigned long long previous = 0, idle_previous = 0;
    std::ifstream stat("/proc/stat");
    std::string tag;
    unsigned long long u = 0, n = 0, s = 0, idle = 0, wait = 0, irq = 0, soft = 0, steal = 0;
    stat >> tag >> u >> n >> s >> idle >> wait >> irq >> soft >> steal;
    auto total = u + n + s + idle + wait + irq + soft + steal;
    double cpu =
        total > previous
            ? 100.0 * (1.0 - double(idle + wait - idle_previous) / double(total - previous))
            : 0;
    previous = total;
    idle_previous = idle + wait;
    std::ifstream mem("/proc/meminfo");
    unsigned long long mt = 0, ma = 0, value;
    std::string unit, line;
    while (std::getline(mem, line)) {
        std::istringstream row(line);
        row >> tag >> value >> unit;
        if (tag == "MemTotal:")
            mt = value;
        if (tag == "MemAvailable:")
            ma = value;
    }
    struct statvfs disk{};
    statvfs("/", &disk);
    char text[256];
    snprintf(text, sizeof text, "CPU %.0f%%   ·   内存 %.1f / %.1f GiB   ·   磁盘可用 %.1f GiB",
             std::clamp(cpu, 0.0, 100.0), double(mt - ma) / 1048576, double(mt) / 1048576,
             double(disk.f_bavail) * disk.f_frsize / 1073741824);
    gtk_label_set_text(GTK_LABEL(summary), text);
    return G_SOURCE_CONTINUE;
}
static void build() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), popup ? "mnOS 应用与工具" : "mnOS 系统设置");
    gtk_window_set_icon_name(GTK_WINDOW(window), "mnos");
    css(window, "mnos");
    GdkMonitor *monitor = gdk_display_get_primary_monitor(gdk_display_get_default());
    if (!monitor)
        monitor = gdk_display_get_monitor(gdk_display_get_default(), 0);
    GdkRectangle area{0, 0, 1280, 720};
    if (monitor)
        gdk_monitor_get_workarea(monitor, &area);
    gtk_window_set_default_size(GTK_WINDOW(window), std::min(popup ? 560 : 920, area.width),
                                std::min(popup ? 620 : 640, area.height));
    if (popup) {
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
        gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
        gtk_window_move(GTK_WINDOW(window), area.x, std::max(area.y, 34));
    } else
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) {
                         if (gtk_main_level())
                             gtk_main_quit();
                     }),
                     nullptr);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key), nullptr);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(lost_focus), nullptr);
    auto *shell = box(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), shell);
    search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "搜索应用、工具和设置…");
    margin(search, 12);
    gtk_box_pack_start(GTK_BOX(shell), search, FALSE, FALSE, 0);
    g_signal_connect(search, "changed", G_CALLBACK(changed), nullptr);
    g_signal_connect(search, "activate", G_CALLBACK(activate_first), nullptr);
    auto *body = box(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(shell), body, TRUE, TRUE, 0);
    auto *nav = box(GTK_ORIENTATION_VERTICAL, 2);
    css(nav, "categories");
    gtk_widget_set_size_request(nav, 158, -1);
    margin(nav, 6);
    gtk_box_pack_start(GTK_BOX(body), nav, FALSE, FALSE, 0);
    GSList *radio = nullptr;
    for (size_t i = 0; i < G_N_ELEMENTS(groups); ++i) {
        auto *button = gtk_radio_button_new(radio);
        radio = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
        css(button, "category");
        auto *row = box(GTK_ORIENTATION_HORIZONTAL, 8);
        auto *icon = gtk_image_new_from_icon_name(group_icons[i], GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
        gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), label(groups[i]), FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(button), row);
        g_object_set_data(G_OBJECT(button), "category", const_cast<char *>(groups[i]));
        g_signal_connect(button, "toggled", G_CALLBACK(+[](GtkToggleButton *b, gpointer) {
                             if (!gtk_toggle_button_get_active(b))
                                 return;
                             selected =
                                 static_cast<char *>(g_object_get_data(G_OBJECT(b), "category"));
                             if (search)
                                 gtk_entry_set_text(GTK_ENTRY(search), "");
                             if (list)
                                 gtk_list_box_invalidate_filter(GTK_LIST_BOX(list));
                         }),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);
        if ((!popup && i == 2) || (popup && i == 0))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    auto *scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(body), scroller, TRUE, TRUE, 0);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
    gtk_container_add(GTK_CONTAINER(scroller), list);
    for (auto &e : entries) {
        auto *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "entry", &e);
        auto *inner = box(GTK_ORIENTATION_HORIZONTAL, 10);
        margin(inner, 8);
        gtk_container_add(GTK_CONTAINER(row), inner);
        GIcon *icon = e.app ? g_app_info_get_icon(e.app) : nullptr;
        auto *image = icon ? gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU)
                           : gtk_image_new_from_icon_name(e.icon.c_str(), GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
        gtk_box_pack_start(GTK_BOX(inner), image, FALSE, FALSE, 0);
        auto *texts = box(GTK_ORIENTATION_VERTICAL, 3);
        auto *title = label(e.name);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(texts), title, FALSE, FALSE, 0);
        if (!popup) {
            auto *desc = label(e.description);
            css(desc, "muted");
            gtk_label_set_ellipsize(GTK_LABEL(desc), PANGO_ELLIPSIZE_END);
            gtk_box_pack_start(GTK_BOX(texts), desc, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(inner), texts, TRUE, TRUE, 0);
        gtk_widget_set_tooltip_text(row, e.description.c_str());
        gtk_container_add(GTK_CONTAINER(list), row);
    }
    gtk_list_box_set_filter_func(GTK_LIST_BOX(list), filter, nullptr, nullptr);
    g_signal_connect(list, "row-activated",
                     G_CALLBACK(+[](GtkListBox *, GtkListBoxRow *r, gpointer) {
                         launch(*static_cast<Entry *>(g_object_get_data(G_OBJECT(r), "entry")));
                     }),
                     nullptr);
    auto *footer = box(GTK_ORIENTATION_HORIZONTAL, 8);
    margin(footer, 10);
    css(footer, "footer");
    gtk_box_pack_start(GTK_BOX(shell), footer, FALSE, FALSE, 0);
    summary = label(popup ? "mnOS  ·  " + std::string(g_get_user_name()) : "");
    css(summary, "muted");
    gtk_box_pack_start(GTK_BOX(footer), summary, TRUE, TRUE, 0);
    auto *about = gtk_button_new_from_icon_name("help-about", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text(about, "关于 mnOS");
    gtk_box_pack_start(GTK_BOX(footer), about, FALSE, FALSE, 0);
    g_signal_connect(
        about, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) {
            GtkWidget *d =
                gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_CLOSE, "mnOS · 双启动预览版");
            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(d),
                "当前是 Linux 工具环境。\nmnOS 系统中心与工具菜单以 C++ 编写；内核另有 "
                "C/汇编自研启动项。\nLinux、Xfce、GTK、驱动和第三方应用保留各自来源及许可。\nhelp "
                "为英文帮助，help -cn "
                "为中文帮助。\n普通用户可编辑文件、使用应用和修改个人设置；系统级修改需要认证。");
            bool old = popup;
            popup = false;
            gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(d);
            popup = old;
        }),
        nullptr);
    if (!popup) {
        metrics(nullptr);
        g_timeout_add_seconds(2, metrics, nullptr);
    }
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(search);
}
static const char *style = R"CSS(
.mnos { background: #1b2026; color: #e6eaf0; font-family: Inter, "Noto Sans CJK SC", sans-serif; font-size: 13px; }
.mnos entry { border-radius: 4px; min-height: 28px; background: #252c34; border-color: #3a454f; }
.mnos list { background: #1b2026; }
.mnos row { border-radius: 3px; }
.mnos row:hover { background: #2a333c; }
.mnos row:selected { background: #31443f; }
.mnos .category { border: 0; box-shadow: none; background: transparent; padding: 9px 7px; border-radius: 3px; }
.mnos .category:checked { background: #303d3e; color: #a2d7ca; }
.mnos .category:hover { background: #29323a; }
.mnos .categories { border-right: 1px solid #323941; }
.mnos .muted { color: #b3bdc7; font-size: 12px; }
.mnos .footer { border-top: 1px solid #323941; padding-top: 5px; }
)CSS";
int main(int argc, char **argv) {
    try {
        popup = fs::path(argv[0]).filename() == "mnos-tools";
        catalog("/usr/share/mnos/tools.json");
        if (argc == 3 && std::string(argv[1]) == "--print-help") {
            // Only run the fixed help invocation from the installed catalog.
            // This console path needs no display and never adds sudo/a shell.
            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry &e) {
                return e.command.size() >= 2 && e.command[0] == "@terminal" &&
                       e.command[1] == argv[2];
            });
            if (it == entries.end()) {
                std::cerr << "No installed manual or catalog help for: " << argv[2] << '\n';
                return 2;
            }
            auto args = it->command;
            args.erase(args.begin());
            args[0] = executable(args[0]);
            if (args[0].empty()) {
                std::cerr << "Tool is not installed: " << argv[2] << '\n';
                return 2;
            }
            std::vector<char *> av;
            for (auto &a : args) av.push_back(a.data());
            av.push_back(nullptr);
            execv(av[0], av.data());
            return 1;
        }
        catalog("/usr/share/mnos/settings.json", true);
        if (argc == 3 &&
            (std::string(argv[1]) == "--help-tool" || std::string(argv[1]) == "--describe-tool")) {
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry &e) { return e.id == argv[2]; });
            if (it == entries.end()) {
                std::cerr << "Unknown tool ID\n";
                return 2;
            }
            auto args = launch_args(*it);
            if (std::string(argv[1]) == "--describe-tool") {
                for (auto &arg : args)
                    std::cout << arg << '\n';
                return 0;
            }
            std::vector<char *> av;
            for (auto &a : args)
                av.push_back(a.data());
            av.push_back(nullptr);
            execv(av[0], av.data());
            return 1;
        }
        if (argc > 1 && std::string(argv[1]) == "--welcome")
            return 0; // Do not obscure the desktop at login.
        testing = argc > 1 && std::string(argv[1]) == "--self-test";
        if (argc > 1 && !testing) {
            std::cerr << "Usage: mnos-tools [--help-tool ID] | mnos-center\n";
            return 2;
        }
        gtk_init(nullptr, nullptr);
        discover();
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
        });
        auto *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, style, -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        build();
        if (testing) {
            for (const char *path : {"/usr/share/backgrounds/mnos/default.svg",
                                     "/usr/share/backgrounds/mnos/coast.png",
                                     "/usr/share/icons/hicolor/scalable/apps/mnos.svg"}) {
                GError *error = nullptr;
                auto *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, 320, 180, TRUE, &error);
                if (!pixbuf) {
                    std::string message = error ? error->message : "Image decode failed";
                    g_clear_error(&error);
                    throw std::runtime_error(message);
                }
                g_object_unref(pixbuf);
            }
            std::set<std::string> ids;
            size_t count = 0;
            for (auto &e : entries)
                if (!e.app) {
                    if (!ids.insert(e.id).second)
                        throw std::runtime_error("Duplicate catalog name");
                    launch_args(e);
                    ++count;
                }
            gtk_entry_set_text(GTK_ENTRY(search), "nmap");
            unsigned visible = 0;
            GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
            for (auto *n = rows; n; n = n->next)
                if (gtk_widget_get_child_visible(GTK_WIDGET(n->data))) {
                    auto *entry = static_cast<Entry *>(g_object_get_data(G_OBJECT(n->data), "entry"));
                    if (entry->search.find("nmap") == std::string::npos)
                        throw std::runtime_error("Search retained a stale result before Enter");
                    ++visible;
                }
            g_list_free(rows);
            if (!visible)
                throw std::runtime_error("Search did not find Nmap");
            std::cout
                << "C++ desktop: " << count
                << " catalog commands resolve; XDG applications, categories and search PASS\n";
            gtk_widget_destroy(window);
            return 0;
        }
        gtk_main();
        for (auto &e : entries)
            if (e.app)
                g_object_unref(e.app);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
