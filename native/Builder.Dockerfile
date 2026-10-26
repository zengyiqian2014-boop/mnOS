# Native kernel build and QA only. These host packages do not enter the ISO.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc g++ make libc6-dev zlib1g-dev clang-19 lld-19 llvm-19 \
    qemu-system-x86 qemu-system-arm qemu-efi-aarch64 ovmf \
    grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools dosfstools \
    ca-certificates curl xz-utils bzip2 bc bison flex cpio rsync libelf-dev libssl-dev \
    gcc-riscv64-linux-gnu gcc-riscv64-unknown-elf device-tree-compiler \
    && rm -rf /var/lib/apt/lists/*
