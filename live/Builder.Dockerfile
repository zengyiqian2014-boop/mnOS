FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN case "$(dpkg --print-architecture)" in \
      amd64) packages='grub-pc-bin grub-efi-amd64-bin qemu-system-x86 ovmf' ;; \
      arm64) packages='grub-efi-arm64-bin qemu-system-arm qemu-efi-aarch64' ;; \
      *) exit 2 ;; esac \
    && apt-get update && apt-get install -y --no-install-recommends squashfs-tools xorriso \
    $packages grub-common mtools dosfstools python3 qemu-utils g++ jq \
    python3-gi gir1.2-gtk-3.0 xvfb fonts-noto-cjk \
    && rm -rf /var/lib/apt/lists/*
