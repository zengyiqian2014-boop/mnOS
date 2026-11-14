#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run ONLY in a disposable container: this creates test users and installs PAM.
set -euo pipefail
[ "${MNOS_AUTH_TEST_CONTAINER:-}" = 1 ] && [ "$(id -u)" = 0 ] || exit 2
cd /source/live/src
make -j4 OUT=/tmp/auth-build
make OUT=/tmp/auth-build install
install -Dm644 ../overlay/etc/pam.d/mnos-security-setup /etc/pam.d/mnos-security-setup
install -Dm644 ../overlay/usr/share/pam-configs/mnos-2fa /usr/share/pam-configs/mnos-2fa
install -Dm644 ../overlay/usr/lib/sysusers.d/mnos-login.conf /usr/lib/sysusers.d/mnos-login.conf
systemd-sysusers /usr/lib/sysusers.d/mnos-login.conf
install -d -o login-process -g login-process -m 700 /var/lib/mnos-login-process
useradd -m -u 1000 demo
useradd -m -u 1001 other
printf 'demo:Test-only-password-491\nother:Test-only-password-491\n' | chpasswd
DEBIAN_FRONTEND=noninteractive pam-auth-update --package --force
cat > /etc/pam.d/mnos-auth-test <<'EOF'
auth include common-auth
account include common-account
password include common-password
EOF
cc -std=c11 -O2 -g -Wall -Wextra -Werror -I. -I../../native/include ../tests/authentication.c login-protocol.c -lpam -lcrypto -o /tmp/authentication-test
/tmp/authentication-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I. -I../../native/include -I../../native/vendor/qrcodegen \
    ../tests/authentication-ui.cpp /tmp/auth-build/security/*.o \
    $(pkg-config --cflags --libs gtk+-3.0 zbar) -lpam -lcrypto -o /tmp/authentication-ui
xvfb-run -a -s '-screen 0 1024x768x24' /tmp/authentication-ui
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I. ../tests/face-challenge.cpp -o /tmp/face-challenge-test
/tmp/face-challenge-test
if [ -f /source/live/build/face-models/yunet.onnx ]; then
    install -d /usr/share/mnos/face
    install -m 644 /source/live/build/face-models/* /usr/share/mnos/face/
    (cd /usr/share/mnos/face && sha256sum --check /source/live/face/SOURCES.sha256)
    c++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I. -I../../native/include \
        -I../../native/vendor/qrcodegen -I../../native/vendor/monocypher ../tests/face-storage.cpp \
        /tmp/auth-build/security/*.o $(pkg-config --cflags --libs gtk+-3.0 opencv4) -pthread -lpam -o /tmp/face-storage-test
    /tmp/face-storage-test
else
    echo 'SKIP: model loading/storage test needs verified assets; run live/fetch-face-models.sh first.'
fi
