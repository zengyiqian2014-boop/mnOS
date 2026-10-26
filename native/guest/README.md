# Original software Linux machine

See [edition boundaries and management](../../EDITIONS.md). The CPU/MMU/SBI
implementation is `src/guest.c`, shared unchanged between the hosted test
harness and both native hosts. This directory contains the **guest** build recipe,
not a second implementation of the Linux kernel.

Build dependencies are installed by `native/Builder.Dockerfile`. Run:

```sh
cd native
guest/build.sh
cc -O2 -Iinclude tools/guest-run.c src/guest.c -o build/guest-run
build/guest-run build/guest/Image build/guest/machine.dtb
GUEST=1 installer/build.sh
```

The build pins Linux 6.12.114, BusyBox 1.37.0 and musl 1.2.5 by archive hash.
The LP64 soft-float GCC multilib is used intentionally; an LP64D libc/runtime is
not ABI-compatible. Guest instructions are RV64IMAC, even on AMD64/ARM64 hosts.
The guest rootfs contains no Python or Java. It uses upstream BusyBox applets,
not placeholder mnOS commands. Obtain the real list with `busybox --list`.

Linux is GPL-2.0-only; BusyBox is GPL-2.0-only; musl is MIT; GCC runtime components
use GPLv3 with the GCC Runtime Library Exception. Preserve the archive hashes,
kernel and BusyBox configs and complete matching source archives when publishing
the bundled binary. Native/interpreter changes are GPL-3.0-or-later.

Guest RAM is bounded and every guest memory access is translated and range
checked. A guest address never becomes a host pointer. No filesystem/syscall
passthrough or host shell execution exists. Guest root is an isolated guest
identity, not host root. The interpreter is nevertheless kernel-resident and
experimental; it has not received an independent security audit.
