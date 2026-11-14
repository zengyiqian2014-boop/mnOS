// SPDX-License-Identifier: GPL-3.0-or-later
#include "mnos-locale.h"
#include <gtk/gtk.h>
#include <cstring>
#include <iostream>

static constexpr const char *address = "report.mnos@proton.me";
static const char *intro() {
    return mnos_text("Report bugs and hardware compatibility issues at any time.",
                     "如果遇到 Bug 或硬件兼容性问题，可随时反馈。");
}
static const char *details() {
    return mnos_text(
        "Include the mnOS version and edition, processor architecture, device or virtual machine model, "
        "what you expected, and steps to reproduce the problem. Screenshots and relevant log excerpts can help.\n\n"
        "Review attachments before sending. Do not include passwords, tokens, authenticator secrets or recovery codes. "
        "This window does not collect logs or send reports automatically.",
        "请说明 mnOS 版本与发行版、处理器架构、设备或虚拟机型号、预期结果，以及复现步骤。"
        "可以附上截图和相关日志片段。\n\n"
        "发送前请检查附件，不要包含密码、令牌、验证器密钥或恢复码。此窗口不会自动收集日志或发送报告。");
}
static GtkWidget *paragraph(const char *text) {
    auto *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), true);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 64);
    return label;
}
int main(int argc, char **argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "--text"))) {
        std::cout << intro() << '\n' << address << "\n\n" << details() << '\n';
        return 0;
    }
    if (argc != 1) { std::cerr << "Usage: mnos-support [--text|--help]\n"; return 2; }
    gtk_init(&argc, &argv);
    auto *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), mnos_text("mnOS System Support", "mnOS 系统支持"));
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 380);
    auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(box), 28);
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_box_pack_start(GTK_BOX(box), paragraph(intro()), false, false, 0);
    auto *email = paragraph(address);
    gtk_label_set_selectable(GTK_LABEL(email), true);
    auto *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_scale_new(1.4));
    gtk_label_set_attributes(GTK_LABEL(email), attributes);
    pango_attr_list_unref(attributes);
    gtk_box_pack_start(GTK_BOX(box), email, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), paragraph(details()), true, true, 0);
    auto *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    auto *copy = gtk_button_new_with_label(mnos_text("Copy email address", "复制邮箱"));
    auto *write = gtk_button_new_with_label(mnos_text("Write email", "撰写邮件"));
    auto *status = paragraph("");
    gtk_box_pack_start(GTK_BOX(row), copy, false, false, 0);
    gtk_box_pack_start(GTK_BOX(row), write, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), row, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), status, false, false, 0);
    g_signal_connect(copy, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), address, -1);
        gtk_label_set_text(GTK_LABEL(data), mnos_text("Email address copied.", "邮箱已复制。"));
    }), status);
    g_signal_connect(write, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        GError *error = nullptr;
        if (!g_app_info_launch_default_for_uri("mailto:report.mnos@proton.me?subject=mnOS%20feedback", nullptr, &error)) {
            gtk_label_set_text(GTK_LABEL(data), mnos_text("No email application opened. Copy the address and use your usual email service.",
                "未能打开邮件应用。请复制邮箱，在常用邮件服务中发送。"));
            g_clear_error(&error);
        }
    }), status);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(copy);
    gtk_main();
    return 0;
}
