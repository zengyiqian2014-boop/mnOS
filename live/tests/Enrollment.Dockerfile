FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends g++ libpwquality-tools cracklib-runtime wamerican \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --force sudo && groupadd --force netdev && groupadd --force plugdev
COPY overlay/etc/security/pwquality.conf.d/50-mnos.conf /etc/security/pwquality.conf.d/50-mnos.conf
COPY src/first-boot.cpp src/process.hpp /src/
COPY overlay/usr/lib/mnos/account-preflight /usr/lib/mnos/account-preflight
RUN update-cracklib && mkdir -p /usr/lib/mnos \
    && chmod 755 /usr/lib/mnos/account-preflight \
    && g++ -std=c++17 -O2 -Wall -Wextra -Werror -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
       -fPIE -pie -Wl,-z,relro,-z,now /src/first-boot.cpp -o /usr/lib/mnos/first-boot
