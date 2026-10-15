# Build/QA tools only. None of these hosted packages enter the native ISO.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-19 lld-19 llvm-19 make gcc libc6-dev qemu-system-arm qemu-efi-aarch64 \
    mtools dosfstools xorriso g++ \
    && rm -rf /var/lib/apt/lists/*
