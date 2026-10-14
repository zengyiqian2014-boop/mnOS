FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends squashfs-tools xorriso \
    grub-pc-bin grub-efi-amd64-bin grub-common mtools dosfstools python3 \
    qemu-system-x86 ovmf python3-gi gir1.2-gtk-3.0 xvfb fonts-noto-cjk \
    && rm -rf /var/lib/apt/lists/*
