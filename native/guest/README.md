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

Before startup, `linux memory auto` selects up to 1 GiB from available host RAM.
`linux memory MIB` accepts an explicit limit from 128 MiB to 512 GiB; startup
rejects limits above free host memory minus a 32 MiB reserve. The host uses a
sparse page map instead of reserving a contiguous 64 MiB buffer. Pages are zeroed
on allocation, and wiped and freed on stop. The device tree advertises the
selected size to Linux. These controls expand RAM, not the supported devices or ISA.

Guest RAM is bounded and every guest memory access is translated and range
checked. A guest address never becomes a host pointer. No filesystem/syscall
passthrough or host shell execution exists. Guest root is an isolated guest
identity, not host root. The interpreter is nevertheless kernel-resident and
experimental; it has not received an independent security audit.

Linux simulation is disabled by default and after every host session boundary.
The native launcher and `linux open APP` request a GUI confirmation before any
guest RAM allocation or execution. Settings (F7, Linux 模拟 / L) can disable it
immediately; this stops the interpreter and wipes private RAM and queued input.
Closing the window only hides it. No enable flag is persisted.

When Linux executes WFI, the native host yields the interpreter instead of
executing its full instruction budget. Input and the next guest timer wake it;
sleeping jobs still progress. `linux pause` remains an explicit indefinite pause.
RAM is retained while idle; `linux stop` wipes it. This is CPU idle scheduling,
not removal of every Linux process: Linux always retains init/kernel workers.

APT, a Debian userspace, virtual networking and general desktop applications are
still missing from this RV64 guest. APT is a required compatibility milestone,
not something the BusyBox command inventory currently implements. The Linux
Full/Lite editions already ship real APT; see `help apt` / `help -cn apt` there.

`make host-tests` exercises consent, cancellation, failed starts, queued apps,
pause/resume, disable, logout and account changes under ASan/UBSan. Set
`MNOS_TEST_LINUX_GUEST=1` when running `build/desktop-vm` to test actual Linux
execution through the native GUI on AMD64 and ARM64, including consent buttons,
the Settings switch and clearing guest files across disable/re-enable.
