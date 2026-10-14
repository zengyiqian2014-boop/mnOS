mnOS 1.7 桌面预览版

mnOSv1.7-desktop-qemu.iso：可启动 ISO，约 86 MiB。
kernel-qemu.config：镜像实际使用的 Linux 6.13.3 内核配置。
SHA256SUMS：下载校验文件。

镜像由源码提交 38cdaebca5c990f0b718b56384f5d17e374fd682 构建。
已在 QEMU 中验证 BIOS、UEFI、键盘、鼠标和终端操作。建议分配 1 GiB 内存。

运行：
  ./run.sh releases/mnOSv1.7-desktop-qemu.iso

校验（进入 releases 目录后执行）：
  sha256sum -c SHA256SUMS

文件保存在内存中，关机或重启后不保留。
