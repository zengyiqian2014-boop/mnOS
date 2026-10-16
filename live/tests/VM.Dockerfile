FROM debian:13-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    g++ qemu-system-x86 qemu-system-arm qemu-utils ovmf qemu-efi-aarch64 \
    && rm -rf /var/lib/apt/lists/*
