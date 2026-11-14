FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f AS common
ARG DEBIAN_FRONTEND=noninteractive
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod 755 /usr/sbin/policy-rc.d \
    && sed -i 's/Components: main/Components: main contrib non-free non-free-firmware/' /etc/apt/sources.list.d/debian.sources
COPY packages /tmp/packages
RUN apt-get update \
    && sed '/^#/d; /^$/d' /tmp/packages/lite.txt "/tmp/packages/$(dpkg --print-architecture).txt" \
       /tmp/packages/hardware.txt "/tmp/packages/hardware-$(dpkg --print-architecture).txt" \
       | sed '/^blueman$/d' | xargs apt-get install -y --no-install-recommends \
    && ! dpkg-query -W -f='${db:Status-Status}\t${binary:Package}\n' | grep -E '^installed[[:space:]]+(python|libpython)' \
    && sed -i 's|http://deb.debian.org|https://deb.debian.org|g' /etc/apt/sources.list.d/debian.sources \
    && rm -rf /var/lib/apt/lists/* /tmp/packages
FROM common AS components
RUN --mount=type=secret,id=build_ca,mode=0444 \
    if [ -r /run/secrets/build_ca ]; then printf 'Acquire::https::CaInfo "/run/secrets/build_ca";\n' > /etc/apt/apt.conf.d/99-mnos-build-ca; fi \
    && apt-get update && apt-get install -y --no-install-recommends libgtk-3-dev libjson-glib-dev
COPY . /source
RUN make -C /source/src -j4 OUT=/build FLAVOR=lite SECURITY_SRC=/source/build/security-src \
    && make -C /source/src OUT=/build FLAVOR=lite SECURITY_SRC=/source/build/security-src DESTDIR=/compiled install \
    && bash /source/update/build-packages.sh "$(cat /source/VERSION)" /compiled /debs
FROM common AS final
ARG PROFILE=installer
RUN --mount=type=bind,source=overlay,target=/tmp/overlay \
    --mount=from=components,source=/compiled,target=/tmp/compiled \
    --mount=from=components,source=/debs,target=/tmp/mnos-packages \
    --mount=type=bind,source=configure.sh,target=/tmp/mnos-configure \
    --mount=type=bind,source=src,target=/tmp/mnos-src \
    --mount=type=bind,source=keys,target=/tmp/mnos-keys \
    --mount=type=bind,source=build/native-src,target=/tmp/mnfs-src \
    cp -a /tmp/compiled/. / \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now /tmp/mnos-src/kernel.c -o /usr/bin/kernel \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now /tmp/mnos-src/kernel-admin.c -o /usr/sbin/mnos-kernel-admin \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 -Wl,-z,relro,-z,now /tmp/mnos-src/net-admin.c -o /usr/lib/mnos/net-admin \
    && cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I/tmp/mnfs-src /tmp/mnfs-src/mnfs-tool.c /tmp/mnfs-src/mnfs.c -o /usr/bin/mnfs \
    && install -m644 /tmp/mnos-keys/mnos-archive.gpg /usr/share/keyrings/mnos-archive.gpg \
    && install -m644 /tmp/mnos-keys/microsoft-code.asc /usr/share/keyrings/microsoft-code.asc \
    && printf 'deb [arch=%s signed-by=/usr/share/keyrings/microsoft-code.asc] https://packages.microsoft.com/repos/code stable main\n' "$(dpkg --print-architecture)" > /etc/apt/sources.list.d/microsoft-code.list \
    && cp -a --no-preserve=ownership /tmp/overlay/. / \
    && (cd /tmp/overlay && find . -type d -print0 | xargs -0 -I{} chmod 755 /{}) \
    && (cd /tmp/overlay && find . -type f -print0 | xargs -0 -I{} chmod 644 /{}) \
    && MNOS_BUILD_ROOT=1 MNOS_PROFILE="$PROFILE" MNOS_FLAVOR=lite sh /tmp/mnos-configure \
    && dpkg --force-confold -i /tmp/mnos-packages/*.deb \
    && ! dpkg-query -W -f='${db:Status-Status}\t${binary:Package}\n' | grep -E '^installed[[:space:]]+(python|libpython)' \
    && rm /usr/sbin/policy-rc.d && rm -rf /var/lib/apt/lists/* \
    && rm -f /var/lib/dbus/machine-id /etc/machine-id /etc/ssh/ssh_host_* \
    && touch /etc/machine-id && ln -s /etc/machine-id /var/lib/dbus/machine-id \
    && find /var/log -type f -exec truncate -s 0 {} +
CMD ["/sbin/init"]
