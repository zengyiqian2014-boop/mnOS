// SPDX-License-Identifier: GPL-3.0-or-later
// Run under Xvfb; exercises real GTK signals and local NSS account lookup.
#define main installer_entry_point
#include "../src/installer.cpp"
#undef main
#include <cassert>
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    Installer form;
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(form.regional.language), "zh_CN.UTF-8");
    for (const char *reserved : {"root", "daemon", "nobody"}) {
        gtk_entry_set_text(GTK_ENTRY(form.name), reserved);
        assert(std::string(gtk_label_get_text(GTK_LABEL(form.status))) ==
            "此用户名已被系统占用，请填写一个其他的");
    }
    std::string account = "mnos_form_" + std::string(80, 'a');
    gtk_entry_set_text(GTK_ENTRY(form.name), account.c_str());
    assert(account == gtk_entry_get_text(GTK_ENTRY(form.name)));
    assert(std::string(gtk_label_get_text(GTK_LABEL(form.status))).find("已被系统占用") == std::string::npos);
    for (const auto &secret : {std::string("q7"), std::string(511, 'q')}) {
        gtk_entry_set_text(GTK_ENTRY(form.password), secret.c_str());
        gtk_entry_set_text(GTK_ENTRY(form.again), secret.c_str());
        assert(secret == gtk_entry_get_text(GTK_ENTRY(form.password)));
        assert(secret == gtk_entry_get_text(GTK_ENTRY(form.again)));
        assert(!gtk_entry_get_visibility(GTK_ENTRY(form.password)));
    }
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(form.regional.language), "en_US.UTF-8");
    assert(std::string(gtk_button_get_label(GTK_BUTTON(form.refresh))) == "Refresh");
    assert(account == gtk_entry_get_text(GTK_ENTRY(form.name)));
    assert(std::string(gtk_entry_get_text(GTK_ENTRY(form.password))).size() == 511);
    gtk_entry_set_text(GTK_ENTRY(form.name), "root");
    assert(std::string(gtk_label_get_text(GTK_LABEL(form.status))).find("reserved by the system") != std::string::npos);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(form.regional.language), "zh_CN.UTF-8");
    assert(std::string(gtk_button_get_label(GTK_BUTTON(form.refresh))) == "刷新");
    assert(std::string(gtk_label_get_text(GTK_LABEL(form.status))) == "此用户名已被系统占用，请填写一个其他的");
    gtk_entry_set_text(GTK_ENTRY(form.regional.servers), "ntp.aliyun.com ntp.tencent.com");
    auto config = form.regional.value();
    assert(config.language == "zh_CN.UTF-8" && config.servers.size() == 2);
    region::validate(config);
    std::cout << "PASS: occupied-name feedback, clearing feedback, long username and untruncated hidden password fields\n";
}
