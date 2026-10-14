# Dual-boot design

The two boot entries have different roles. `native/` contains an original
freestanding x86_64 kernel with page management, ring-3 isolation, preemptive
tasks, bounded system calls and a framebuffer GUI. Its current implementation
and limits are documented in [native/README.md](../native/README.md).
The practical tool environment uses Debian 13 and signed Debian Linux 6.12 LTS
packages. Firefox, CTF tools and existing hardware drivers run there. Neither
environment is RHEL-compatible or security-certified.

## Lessons adopted from enterprise Linux

The RHEL design references are the public [TuneD guide](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html/monitoring_and_managing_system_status_and_performance/getting-started-with-tuned_monitoring-and-managing-system-status-and-performance)
and [security hardening guide](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html/security_hardening/index).
No subscription credentials, licensed RHEL binaries or user-provided certificates
are distributed. Original integration code is GPL-3.0-or-later.

| Principle | Implementation here | Limit |
| --- | --- | --- |
| Maintain a stable kernel line | Signed Debian 6.12 LTS packages and security repository | No RHEL kABI or support-lifetime promise |
| Tune for a workload | TuneD desktop, virtual-guest and opt-in throughput profiles | Benchmarks depend on hardware; no universal speedup claim |
| Enforce least privilege | Local enrollment, locked root, sudo and Polkit authentication | The local administrator intentionally controls the machine |
| Mandatory access control | Debian AppArmor policy and kernel enforcement | This is not the RHEL SELinux policy |
| Audit administrative actions | auditd watches for identity, sudo, AppArmor and firewall changes | Not a complete CIS or regulatory audit |
| Keep a verified boot path | Debian-signed shim, GRUB and kernel; signature checks remain enabled | Live rootfs lacks dm-verity authentication; verify the release hash |
| Maintain traceable software | Package/source versions, kernel config and wheel checksums | Repository updates can change later builds |

The Linux kernel build checks cover ASLR, strict executable/writable separation, strong
stack protection, hardened usercopy, FORTIFY, signed modules, lockdown, seccomp,
namespaces, cgroups and common hardware drivers. Runtime checks inspect the
active security state rather than displaying a decorative security score.

## Performance

The hardware profile uses `mnos-desktop` (TuneD `desktop`/`balanced`), suitable for
interactive use and laptops. QEMU uses `mnos-virtual`, based on `virtual-guest`
with desktop-sized read-ahead and zram-aware swap settings. `mnos-throughput` is
an opt-in profile for sustained workloads and may increase heat and power use:

```
tuned-adm active
sudo tuned-adm profile mnos-throughput
sudo tuned-adm profile mnos-desktop    # hardware default
sudo tuned-adm profile mnos-virtual    # QEMU default
```

LZ4 zram is capped at half of RAM / 4 GiB. SquashFS uses zstd for decompression
speed. The QEMU launcher uses KVM when available, host CPU features, VirtIO and
a hardware RNG device. TCG is a functional fallback, not a performance baseline
for physical hardware. CPU vulnerability mitigations remain enabled.

## Hardware scope

Both profiles keep the distribution kernel's modular drivers. The hardware ISO
adds Intel/AMD CPU microcode; Intel, AMD and Nouveau graphics firmware; Intel,
Realtek, Atheros/Qualcomm, Broadcom/Cypress, MediaTek/Ralink, Marvell and selected
legacy USB Wi-Fi firmware; Bluetooth; SOF, Intel and Cirrus audio firmware; and
common Ethernet firmware. Wi-Fi is managed by NetworkManager/wpa_supplicant.

This is broad x86_64 coverage, not a guarantee for every device or USB ID. Some
Broadcom b43/STA and Realtek USB adapters require additional firmware or out-of-
tree drivers. The legacy Intel IPW2100/IPW2200 firmware package, which needs a
separate interactive license acknowledgement, is not bundled; modern Intel
Wi-Fi uses the included iwlwifi firmware. NVIDIA proprietary CUDA/OpenCL drivers are not preinstalled;
installing DKMS drivers with Secure Boot may require owner-key enrollment.
ARM and Apple Silicon are not targets of these ISOs.

## Desktop and CTF tools

Xfce provides real window management, display settings and session locking. The
GTK system center launches installed tools and reads actual CPU/memory/disk data.
Cockpit runs in its own network namespace via `cockpit-desktop`, on demand, with
no remote management socket. It uses Polkit for administrative access.

Firefox ESR and most tools come from Debian's signed repositories. mitmproxy
12.2.3 is isolated in `/opt/mnos/mitmproxy`; its wheels come from PyPI, and the
resolved versions/hashes are included under `/usr/share/doc/mnos`. It is not
updated by APT and requires a new image/build or an explicit isolated-venv update.
No scanner, password-testing tool or proxy starts automatically.

## Installation and persistence

Live changes are in RAM. The Calamares installer extracts the original SquashFS
and creates a new account on the selected disk; it does not copy the Live user's
password or files. The enrollment service is conditioned on `boot=live`, so it
does not override installed accounts. Disk changes require the installer's
partition summary confirmation. Keep backups before using the installer.
