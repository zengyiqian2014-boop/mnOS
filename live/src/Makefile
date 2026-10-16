# SPDX-License-Identifier: GPL-3.0-or-later
CXX ?= c++
OUT ?= build
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE
LDFLAGS += -pie -Wl,-z,relro,-z,now
all: $(OUT)/mnos-center $(OUT)/first-boot $(OUT)/mnos-network-policy $(OUT)/wallpaper $(OUT)/installer
$(OUT):
	mkdir -p $@
$(OUT)/mnos-center: desktop.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs gtk+-3.0 gio-unix-2.0 json-glib-1.0)
$(OUT)/first-boot: first-boot.cpp process.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(OUT)/installer: installer.cpp process.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs gtk+-3.0 json-glib-1.0)
$(OUT)/mnos-network-policy: network-policy.cpp process.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(OUT)/wallpaper: wallpaper.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs glib-2.0)
install: all
	install -Dm755 $(OUT)/installer $(DESTDIR)/usr/lib/mnos/installer
	install -Dm755 $(OUT)/mnos-center $(DESTDIR)/usr/bin/mnos-center
	ln -sf mnos-center $(DESTDIR)/usr/bin/mnos-tools
	install -Dm755 $(OUT)/first-boot $(DESTDIR)/usr/lib/mnos/first-boot
	install -Dm755 $(OUT)/wallpaper $(DESTDIR)/usr/lib/mnos/wallpaper
	install -Dm755 $(OUT)/mnos-network-policy $(DESTDIR)/usr/sbin/mnos-network-policy
