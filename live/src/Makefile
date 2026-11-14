# SPDX-License-Identifier: GPL-3.0-or-later
CXX ?= c++
OUT ?= build
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE
LDFLAGS += -pie -Wl,-z,relro,-z,now
FLAVOR ?= full
SECURITY_SRC ?= $(if $(wildcard ../../native/include/auth.h),../../native,../mnos-native)
SECURITY_INCLUDES = -I$(SECURITY_SRC)/include -I$(SECURITY_SRC)/vendor/qrcodegen -I$(SECURITY_SRC)/vendor/monocypher
SECURITY_CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIC $(SECURITY_INCLUDES)
SECURITY_OBJECTS = $(OUT)/security/password.o $(OUT)/security/factor.o $(OUT)/security/monocypher.o $(OUT)/security/login-protocol.o
SECURITY_UI_OBJECTS = $(SECURITY_OBJECTS) $(OUT)/security/qrcodegen.o
PAM_LIBDIR ?= /usr/lib/$(shell $(CC) -dumpmachine)/security
all: $(OUT)/mnos-center $(OUT)/first-boot $(OUT)/mnos-network-policy $(OUT)/wallpaper $(OUT)/installer $(OUT)/mnos-help $(OUT)/mnos-pkg $(OUT)/mnos-admin $(OUT)/mnos-security
all: $(OUT)/mnos-session
all: $(OUT)/mnos
$(OUT)/mnos: mnos.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(OUT)/mnos-session: session.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
ifeq ($(FLAVOR),full)
all: $(OUT)/login-process $(OUT)/login-helper $(OUT)/pam_mnos_2fa.so $(OUT)/mnos-face-preview
endif
$(OUT)/mnos-face-preview: face-preview.cpp face-challenge.hpp security-ui.hpp $(SECURITY_UI_OBJECTS)
	$(CXX) $(CXXFLAGS) -Wno-misleading-indentation $(CPPFLAGS) $(SECURITY_INCLUDES) face-preview.cpp $(SECURITY_UI_OBJECTS) -o $@ $(LDFLAGS) -pthread $$(pkg-config --cflags --libs gtk+-3.0 opencv4)
$(OUT)/security:
	mkdir -p $@
$(OUT)/security/password.o: $(SECURITY_SRC)/src/password.c | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -c $< -o $@
$(OUT)/security/factor.o: $(SECURITY_SRC)/src/factor.c | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -c $< -o $@
$(OUT)/security/monocypher.o: $(SECURITY_SRC)/vendor/monocypher/monocypher.c | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -c $< -o $@
$(OUT)/security/qrcodegen.o: $(SECURITY_SRC)/vendor/qrcodegen/qrcodegen.c | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -c $< -o $@
$(OUT)/security/login-protocol.o: login-protocol.c login-protocol.h | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -c $< -o $@
$(OUT)/security/login-library.o: login-process.c login-protocol.h | $(OUT)/security
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -DMNOS_LOGIN_LIBRARY -c $< -o $@
$(OUT)/login-process: login-process.c $(SECURITY_OBJECTS)
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) $^ -o $@ $(LDFLAGS)
$(OUT)/login-helper: login-helper.c $(OUT)/security/login-library.o $(SECURITY_OBJECTS)
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) $^ -o $@ $(LDFLAGS) -lpam
$(OUT)/pam_mnos_2fa.so: pam-mnos-2fa.c $(OUT)/security/login-protocol.o
	$(CC) $(SECURITY_CFLAGS) $(CPPFLAGS) -shared $^ -o $@ -Wl,-z,relro,-z,now -lpam
$(OUT)/mnos-security: security.cpp security-ui.hpp $(SECURITY_UI_OBJECTS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SECURITY_INCLUDES) security.cpp $(SECURITY_UI_OBJECTS) -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs gtk+-3.0)
$(OUT)/mnos-admin: admin.c | $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 $< -o $@ $(LDFLAGS)
$(OUT):
	mkdir -p $@
$(OUT)/mnos-help: help.c | $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 $< -o $@ $(LDFLAGS)
$(OUT)/mnos-pkg: package.c | $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 $< -o $@ $(LDFLAGS)
$(OUT)/mnos-center: desktop.cpp command-index.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs gtk+-3.0 gio-unix-2.0 json-glib-1.0)
$(OUT)/first-boot: first-boot.cpp process.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(OUT)/installer: installer.cpp process.hpp security-ui.hpp $(SECURITY_UI_OBJECTS) | $(OUT)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SECURITY_INCLUDES) installer.cpp $(SECURITY_UI_OBJECTS) -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs gtk+-3.0 json-glib-1.0)
$(OUT)/mnos-network-policy: network-policy.cpp process.hpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)
$(OUT)/wallpaper: wallpaper.cpp | $(OUT)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $$(pkg-config --cflags --libs glib-2.0)
install: all
	install -Dm755 $(OUT)/mnos $(DESTDIR)/usr/bin/mnos
	install -Dm755 $(OUT)/mnos-session $(DESTDIR)/usr/bin/mnos-session
ifeq ($(FLAVOR),full)
	install -Dm755 $(OUT)/mnos-face-preview $(DESTDIR)/usr/bin/mnos-face-preview
	install -Dm755 $(OUT)/login-process $(DESTDIR)/usr/lib/mnos/login-process
	install -Dm4755 $(OUT)/login-helper $(DESTDIR)/usr/lib/mnos/login-helper
	install -Dm644 $(OUT)/pam_mnos_2fa.so $(DESTDIR)$(PAM_LIBDIR)/pam_mnos_2fa.so
endif
	install -Dm755 $(OUT)/mnos-security $(DESTDIR)/usr/bin/mnos-security
	install -Dm755 $(OUT)/mnos-admin $(DESTDIR)/usr/bin/mnos-admin
	install -Dm755 $(OUT)/mnos-help $(DESTDIR)/usr/bin/mnos-help
	install -Dm755 $(OUT)/mnos-pkg $(DESTDIR)/usr/bin/mnos-pkg
	install -Dm755 $(OUT)/installer $(DESTDIR)/usr/lib/mnos/installer
	install -Dm755 $(OUT)/mnos-center $(DESTDIR)/usr/bin/mnos-center
	ln -sf mnos-center $(DESTDIR)/usr/bin/mnos-tools
	install -Dm755 $(OUT)/first-boot $(DESTDIR)/usr/lib/mnos/first-boot
	install -Dm755 $(OUT)/wallpaper $(DESTDIR)/usr/lib/mnos/wallpaper
	install -Dm755 $(OUT)/mnos-network-policy $(DESTDIR)/usr/sbin/mnos-network-policy
