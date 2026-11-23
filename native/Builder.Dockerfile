# Native kernel build and QA only. These host packages do not enter the ISO.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN set -e; \
    case "$(dpkg --print-architecture)" in \
      amd64) boot_tools='grub-pc-bin grub-efi-amd64-bin' ;; \
      arm64) boot_tools='grub-efi-arm64-bin' ;; \
      *) echo 'The native builder requires an amd64 or arm64 host.' >&2; exit 1 ;; \
    esac; \
    apt-get update && apt-get install -y --no-install-recommends \
    gcc g++ make libc6-dev zlib1g-dev clang-19 lld-19 llvm-19 \
    qemu-system-x86 qemu-system-arm qemu-efi-aarch64 ovmf \
    $boot_tools grub-common xorriso mtools dosfstools \
    ca-certificates curl xz-utils bzip2 bc bison flex cpio rsync libelf-dev libssl-dev zbar-tools libusbredirparser-dev \
    gcc-riscv64-linux-gnu g++-riscv64-linux-gnu pkg-config gcc-riscv64-unknown-elf device-tree-compiler \
    debootstrap qemu-user-static binfmt-support squashfs-tools libslirp-dev swtpm fontconfig shared-mime-info \
    && rm -rf /var/lib/apt/lists/*
