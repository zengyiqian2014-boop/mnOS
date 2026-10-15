#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check required build options, without claiming formal RHEL compatibility."""
from pathlib import Path
import sys

required = '''SECURITY SECURITY_APPARMOR AUDIT AUDITSYSCALL RANDOMIZE_BASE
STRICT_KERNEL_RWX STRICT_MODULE_RWX STACKPROTECTOR_STRONG HARDENED_USERCOPY
FORTIFY_SOURCE SECURITY_LOCKDOWN_LSM MODULE_SIG SECCOMP SECCOMP_FILTER
CGROUPS NAMESPACES BLK_DEV_INITRD SQUASHFS OVERLAY_FS ZRAM
VIRTIO_PCI VIRTIO_BLK VIRTIO_NET DRM_VIRTIO_GPU
IWLWIFI RTW88 RTW89 ATH9K ATH10K ATH11K MT76_CORE BRCMFMAC USB_RTL8152
R8169 E1000E IGC IGB DRM_AMDGPU DRM_NOUVEAU BLK_DEV_NVME'''.split()
config = dict(line.split('=', 1) for line in Path(sys.argv[1]).read_text().splitlines()
              if line.startswith('CONFIG_') and '=' in line)
if config.get('CONFIG_ARM64') == 'y':
    required += 'ARM64 EFI ACPI ARM_PSCI_FW SERIAL_AMBA_PL011 DRM_PANFROST DRM_LIMA ARCH_ROCKCHIP ARCH_BCM2835 ARCH_QCOM'.split()
else:
    required += ['DRM_I915']
missing = [key for key in required if config.get('CONFIG_' + key) not in ('y', 'm')]
if missing:
    raise SystemExit('Missing required kernel support: ' + ', '.join(missing))
print(f'PASS: {len(required)} kernel hardening, isolation, storage and driver options')
