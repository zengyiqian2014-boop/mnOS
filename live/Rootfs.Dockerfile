FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS common
ARG DEBIAN_FRONTEND=noninteractive
# Keep installed tools' offline manuals, including files omitted from the base
# image before this build. Installing man-db alone does not restore those files.
RUN sed -i '\|^path-exclude /usr/share/man/\*$|d; \|^path-exclude /usr/share/info/\*$|d' /etc/dpkg/dpkg.cfg.d/docker \
    && dpkg-query -S '/usr/share/man/*' '/usr/share/info/*' \
       | sed 's/: \/usr\/share\/.*//' | tr ',' '\n' | sed 's/^ *//; s/ *$//' | sort -u > /tmp/base-manual-packages \
    && printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod 755 /usr/sbin/policy-rc.d \
    && printf 'Acquire::Retries "5";\n' > /etc/apt/apt.conf.d/80-mnos-build-retries \
    && apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && xargs -r apt-get install -y --no-install-recommends --reinstall < /tmp/base-manual-packages \
    && rm /tmp/base-manual-packages \
    && update-ca-certificates \
    && sed -i 's/Components: main/Components: main contrib non-free non-free-firmware/' /etc/apt/sources.list.d/debian.sources \
    && sed -i 's|http://deb.debian.org|https://deb.debian.org|g' /etc/apt/sources.list.d/debian.sources
COPY packages /tmp/packages
RUN --mount=type=secret,id=build_ca \
    if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && printf 'wireshark-common wireshark-common/install-setuid boolean false\niperf3 iperf3/start_daemon boolean false\n' | debconf-set-selections \
    && apt-get update && cat /tmp/packages/common.txt "/tmp/packages/$(dpkg --print-architecture).txt" | xargs -r apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/* /tmp/packages \
    && rm -f /etc/apt/apt.conf.d/99-mnos-build-ca

# Compile our runtime components separately: development headers stay out of the ISO.
FROM common AS components
RUN --mount=type=secret,id=build_ca \
    if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && apt-get update && apt-get install -y --no-install-recommends g++ make pkg-config libgtk-3-dev libjson-glib-dev
COPY src /src
RUN make -C /src -j4 OUT=/build && make -C /src OUT=/build DESTDIR=/compiled install

FROM common AS final
COPY --from=components /compiled/ /
ARG PROFILE
RUN --mount=type=secret,id=build_ca \
    --mount=type=bind,source=packages,target=/tmp/packages \
    --mount=type=bind,source=overlay,target=/tmp/overlay \
    --mount=type=bind,source=configure.sh,target=/tmp/mnos-configure \
    --mount=type=bind,source=src,target=/tmp/mnos-src \
    --mount=type=bind,source=keys,target=/tmp/mnos-keys \
    --mount=type=bind,source=build/native-src,target=/tmp/mnfs-src \
    case "$PROFILE" in installer|hardware|qemu) ;; *) exit 2 ;; esac \
    && if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && test "$(gpg --show-keys --with-colons /tmp/mnos-keys/microsoft-code.asc 2>/dev/null | awk -F: '$1=="fpr" {print $10; exit}')" = BC528686B50D79E339D3721CEB3E94ADBE1229CF \
    && install -m 644 /tmp/mnos-keys/microsoft-code.asc /usr/share/keyrings/microsoft-code.asc \
    && printf 'deb [arch=%s signed-by=/usr/share/keyrings/microsoft-code.asc] https://packages.microsoft.com/repos/code stable main\n' "$(dpkg --print-architecture)" > /etc/apt/sources.list.d/microsoft-code.list \
    && printf 'code code/add-microsoft-repo boolean false\n' | debconf-set-selections \
    && apt-get update && apt-get install -y --no-install-recommends \
       gir1.2-webkit2-4.1 tuned dbus-x11 dbus-user-session xdg-user-dirs librsvg2-common \
    && MNOS_PACKAGE_PROFILE="$PROFILE" \
    && if [ "$PROFILE" = installer ]; then MNOS_PACKAGE_PROFILE=hardware; fi \
    && xargs -r apt-get install -y --no-install-recommends < "/tmp/packages/$MNOS_PACKAGE_PROFILE.txt" \
    && sed '/^#/d; /^$/d' "/tmp/packages/$MNOS_PACKAGE_PROFILE-$(dpkg --print-architecture).txt" | xargs -r apt-get install -y --no-install-recommends \
    && xargs -r apt-get install -y --no-install-recommends < /tmp/packages/ctf.txt \
    && sed '/^#/d; /^$/d' /tmp/packages/development.txt | xargs -r apt-get install -y --no-install-recommends \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
       -Wl,-z,relro,-z,now /tmp/mnos-src/kernel.c -o /usr/bin/kernel \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
       -Wl,-z,relro,-z,now /tmp/mnos-src/kernel-admin.c -o /usr/sbin/mnos-kernel-admin \
    && chown root:root /usr/bin/kernel /usr/sbin/mnos-kernel-admin \
    && chmod 755 /usr/bin/kernel /usr/sbin/mnos-kernel-admin \
    && mkdir -p /usr/lib/mnos \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
       -Wl,-z,relro,-z,now /tmp/mnos-src/net-admin.c -o /usr/lib/mnos/net-admin \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation \
       -fstack-protector-strong -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now -I/tmp/mnfs-src \
       /tmp/mnfs-src/mnfs-tool.c /tmp/mnfs-src/mnfs.c -o /usr/bin/mnfs \
    && python3 -m venv /opt/mnos/mitmproxy \
    && MNOS_PIP_CA=/etc/ssl/certs/ca-certificates.crt \
    && if [ -r /run/secrets/build_ca ]; then MNOS_PIP_CA=/run/secrets/build_ca; fi \
    && PIP_CERT="$MNOS_PIP_CA" /opt/mnos/mitmproxy/bin/pip \
       --disable-pip-version-check download --index-url https://pypi.org/simple \
       --only-binary=:all: --dest /tmp/mnos-wheels 'mitmproxy==12.2.3' \
    && /opt/mnos/mitmproxy/bin/pip --disable-pip-version-check install \
       --no-index --find-links=/tmp/mnos-wheels 'mitmproxy==12.2.3' \
    && mkdir -p /usr/share/doc/mnos \
    && (cd /tmp/mnos-wheels && sha256sum *.whl) > /usr/share/doc/mnos/mitmproxy-wheels.sha256 \
    && /opt/mnos/mitmproxy/bin/pip freeze > /usr/share/doc/mnos/mitmproxy-requirements.txt \
    && ln -s /opt/mnos/mitmproxy/bin/mitmproxy /usr/local/bin/mitmproxy \
    && ln -s /opt/mnos/mitmproxy/bin/mitmdump /usr/local/bin/mitmdump \
    && ln -s /opt/mnos/mitmproxy/bin/mitmweb /usr/local/bin/mitmweb \
    && rm -rf /tmp/mnos-wheels /root/.cache/pip \
    && cp -a --no-preserve=ownership /tmp/overlay/. / \
    && (cd /tmp/overlay && find . -type d -print0 | xargs -0 -I{} chmod 755 /{}) \
    && (cd /tmp/overlay && find . -type f -print0 | xargs -0 -I{} chmod 644 /{}) \
    && MNOS_BUILD_ROOT=1 MNOS_PROFILE="$PROFILE" sh /tmp/mnos-configure \
    && rm /usr/sbin/policy-rc.d && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/apt/apt.conf.d/99-mnos-build-ca \
    && rm -f /etc/apt/apt.conf.d/80-mnos-build-retries \
    && rm -f /var/lib/dbus/machine-id /etc/machine-id /etc/ssh/ssh_host_* \
    && touch /etc/machine-id \
    && ln -s /etc/machine-id /var/lib/dbus/machine-id \
    && rm -f /root/.bash_history /var/lib/systemd/random-seed \
    && find /var/log -type f -exec truncate -s 0 {} +
CMD ["/sbin/init"]
