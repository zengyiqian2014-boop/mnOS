FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS common
ARG DEBIAN_FRONTEND=noninteractive
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod 755 /usr/sbin/policy-rc.d \
    && apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && update-ca-certificates \
    && sed -i 's/Components: main/Components: main contrib non-free non-free-firmware/' /etc/apt/sources.list.d/debian.sources \
    && sed -i 's|http://deb.debian.org|https://deb.debian.org|g' /etc/apt/sources.list.d/debian.sources
COPY packages/common.txt /tmp/common.txt
RUN --mount=type=secret,id=build_ca \
    if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && printf 'wireshark-common wireshark-common/install-setuid boolean false\niperf3 iperf3/start_daemon boolean false\n' | debconf-set-selections \
    && apt-get update && xargs -r apt-get install -y --no-install-recommends < /tmp/common.txt \
    && rm -rf /var/lib/apt/lists/* /tmp/common.txt \
    && rm -f /etc/apt/apt.conf.d/99-mnos-build-ca

FROM common AS final
ARG PROFILE
RUN --mount=type=secret,id=build_ca \
    --mount=type=bind,source=packages,target=/tmp/packages \
    --mount=type=bind,source=overlay,target=/tmp/overlay \
    --mount=type=bind,source=configure.sh,target=/tmp/mnos-configure \
    --mount=type=bind,source=src,target=/tmp/mnos-src \
    --mount=type=bind,source=build/native-src,target=/tmp/mnfs-src \
    case "$PROFILE" in hardware|qemu) ;; *) exit 2 ;; esac \
    && if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && apt-get update && apt-get install -y --no-install-recommends \
       gir1.2-webkit2-4.1 tuned dbus-x11 dbus-user-session xdg-user-dirs librsvg2-common \
    && xargs -r apt-get install -y --no-install-recommends < "/tmp/packages/$PROFILE.txt" \
    && xargs -r apt-get install -y --no-install-recommends < /tmp/packages/ctf.txt \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
       -Wl,-z,relro,-z,now /tmp/mnos-src/kernel.c -o /usr/bin/kernel \
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
    && rm -f /var/lib/dbus/machine-id /etc/machine-id /etc/ssh/ssh_host_* \
    && touch /etc/machine-id \
    && ln -s /etc/machine-id /var/lib/dbus/machine-id \
    && rm -f /root/.bash_history /var/lib/systemd/random-seed \
    && find /var/log -type f -exec truncate -s 0 {} +
CMD ["/sbin/init"]
