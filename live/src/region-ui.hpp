// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "region.hpp"
#include "mnos-locale.h"
#include <gtk/gtk.h>
namespace region {
// Reused in the installer and the installed system's settings window.
struct Fields {
    GtkWidget *grid{}, *language{}, *formats{}, *timezone{}, *servers{}, *preset{};
    GtkWidget *captions[5]{}, *hint{}, *add{};
    explicit Fields(const Config &c = {}) {
        grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
        auto field = [&](GtkWidget *w, int row) {
            captions[row] = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(captions[row]), 0);
            gtk_grid_attach(GTK_GRID(grid), captions[row], 0, row, 1, 1);
            gtk_widget_set_hexpand(w, true);
            gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
        };
        language = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(language), "en_US.UTF-8", "English");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(language), "zh_CN.UTF-8", "简体中文");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(language), c.language.c_str());
        field(language, 0);
        formats = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formats), "en_US.UTF-8", "United States / 美国");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formats), "zh_CN.UTF-8", "China / 中国");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(formats), c.formats.c_str());
        field(formats, 1);
        timezone = gtk_combo_box_text_new_with_entry();
        std::vector<std::string> zones = {"Etc/UTC"};
        std::ifstream table("/usr/share/zoneinfo/zone1970.tab");
        std::string line;
        while (std::getline(table, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line); std::string country, coordinates, name;
            if (row >> country >> coordinates >> name) zones.push_back(name);
        }
        std::sort(zones.begin(), zones.end());
        for (const auto &zone : zones) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(timezone), zone.c_str());
        gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(timezone))), c.timezone.c_str());
        field(timezone, 2);
        servers = gtk_entry_new();
        std::string list;
        for (const auto &host : c.servers) { if (!list.empty()) list += ' '; list += host; }
        gtk_entry_set_text(GTK_ENTRY(servers), list.c_str());
        field(servers, 3);
        preset = gtk_combo_box_text_new();
        for (const auto &p : presets) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(preset), p.host, p.name);
        gtk_combo_box_set_active(GTK_COMBO_BOX(preset), 0);
        field(preset, 4);
        add = gtk_button_new_with_label("");
        gtk_grid_attach(GTK_GRID(grid), add, 2, 4, 1, 1);
        g_signal_connect_swapped(add, "clicked", G_CALLBACK(+[](Fields *s) {
            const char *host = gtk_combo_box_get_active_id(GTK_COMBO_BOX(s->preset));
            if (!host) return;
            std::string existing = gtk_entry_get_text(GTK_ENTRY(s->servers));
            // Google uses leap smear; choosing it starts a separate source set.
            if (!strcmp(host, "time.google.com") || existing.find("google.com") != std::string::npos) existing.clear();
            std::istringstream parts(existing); std::string item;
            while (parts >> item) if (item == host) return;
            if (!existing.empty()) existing += ' ';
            gtk_entry_set_text(GTK_ENTRY(s->servers), (existing + host).c_str());
        }), this);
        hint = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(hint), 0);
        gtk_label_set_line_wrap(GTK_LABEL(hint), true);
        gtk_label_set_max_width_chars(GTK_LABEL(hint), 68);
        gtk_grid_attach(GTK_GRID(grid), hint, 0, 5, 3, 1);
        translate(c.language == "zh_CN.UTF-8");
    }
    void translate(bool cn) {
        const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(formats));
        std::string format = id ? id : "en_US.UTF-8";
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(formats));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formats), "en_US.UTF-8", cn ? "美国" : "United States");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formats), "zh_CN.UTF-8", cn ? "中国" : "China");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(formats), format.c_str());
        id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(preset));
        std::string source = id ? id : "pool.ntp.org";
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(preset));
        for (const auto &p : presets) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(preset), p.host, cn ? p.chinese : p.name);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(preset), source.c_str());
        const char *en[] = {"Language", "Regional formats", "Time zone", "Time servers", "Server presets"};
        const char *zh[] = {"语言", "地区格式", "时区", "时间服务器", "常用服务器"};
        for (unsigned i = 0; i < 5; ++i) gtk_label_set_text(GTK_LABEL(captions[i]), cn ? zh[i] : en[i]);
        gtk_button_set_label(GTK_BUTTON(add), cn ? "添加" : "Add");
        gtk_label_set_text(GTK_LABEL(hint), cn ?
            "地址之间用空格分隔，可直接填写自定义服务器。Google 使用平滑闰秒，选择它会替换其他来源。" :
            "Separate addresses with spaces; custom servers are accepted. Google uses leap smear, so selecting it replaces other sources.");
    }
    Config value() const {
        Config c;
        const char *lang = gtk_combo_box_get_active_id(GTK_COMBO_BOX(language));
        const char *region = gtk_combo_box_get_active_id(GTK_COMBO_BOX(formats));
        c.language = lang ? lang : ""; c.formats = region ? region : "";
        c.timezone = gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(timezone))));
        std::istringstream list(gtk_entry_get_text(GTK_ENTRY(servers)));
        c.servers.clear(); std::string item;
        while (list >> item) c.servers.push_back(item);
        return c;
    }
};
}
