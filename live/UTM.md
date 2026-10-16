# 在 Apple Silicon Mac 上用 UTM 安装

使用完整 ARM64 安装 ISO。桌面、应用、驱动和实验内核都在同一个安装包里。

## 两种苹果虚拟化接口

UTM 向导里的 **“使用 Apple 虚拟化”** 会选择 Apple Virtualization.framework
后端。QEMU 设置里的 **“Use Hypervisor / 使用 Hypervisor”** 则允许 QEMU 使用
Hypervisor.framework（HVF）执行同架构 CPU 指令。两者不是同一个开关。

**取消“使用 Apple 虚拟化”不等于只能使用慢速 CPU 模拟。** Apple Silicon 主机上
选择 ARM64 虚拟化、QEMU 后端并启用 Use Hypervisor，仍可使用 CPU 硬件加速。
虚拟显卡的 GL / VirGL 加速是第三个独立设置。

当前自动验证运行在 Linux 上，涵盖 QEMU 的 KVM / TCG；没有 macOS 执行环境，
不能据此声称 Apple Virtualization.framework 或 UTM/HVF 已通过实测。

2026-10-16 用户反馈：使用同一份 ARM64 安装 ISO，取消“使用 Apple 虚拟化”后
成功进入图形安装器。后续截图显示完整的账号设置界面，并识别到 `/dev/vda` 的
64 GiB 虚拟磁盘。用户侧安装完成及硬盘启动尚待确认；实际 HVF 开关状态没有日志
佐证。这个结果确认该用户的 QEMU 配置能够启动安装器，不能据此认定 Apple 后端
的具体故障位置，或宣布其兼容性已修复。

## 排查配置

1. UTM 新建虚拟机，选择 **虚拟化 → Linux**，取消 **使用 Apple 虚拟化**。
   保留原虚拟机及其磁盘，避免覆盖已有数据。
2. 架构选 ARM64 / AArch64，保留 UEFI；内存 4–8 GiB，磁盘至少 40 GiB。
   ISO 启动方式无需勾选“从内核映像启动”。
3. QEMU 设置保留 **Use Hypervisor**。显示设备先选普通 **virtio-gpu-pci**；
   不选带 `-gl` 或 **GPU Supported** 标记的型号，先关闭 Retina／自动分辨率。
4. 挂载完整 ARM64 安装 ISO，选第一项 **Install mnOS**。安装完成后移除 ISO，
   从硬盘启动。实验内核入口不是安装步骤，暂不具备 Linux 的 PCI/USB/GPU 驱动覆盖。

如果仍无画面，记录实际虚拟机信息页的后端、显示设备及最后的启动输出。
“Display output is not active”只表示当前显示设备没有活动画面，不能单独定位
CPU 虚拟化、内核、显卡驱动或图形会话中的哪一层出了问题。
信息页显示 `QEMU 9.1 ARM Virtual Machine`，意味着这台虚拟机走的是 QEMU 后端；
创建向导中的 Apple 选项截图可能属于另一台尚未创建的虚拟机，要以实际运行配置为准。

## 兼容显示入口

安装预览版 `.1` 的第二项与第一项使用相同显示路径，不能作为独立的故障回退。
修订后的配置给第二项传入 `mnos.graphics=compat`，通过 Xorg modesetting 驱动的
`AccelMethod=none`、软件光标和禁用翻页／GLX 保留基本画面；不使用会关闭 VirtIO
显示驱动的 `nomodeset`。普通 Xorg 会话退出失败时也会尝试这个兼容路径。
这解决了回退入口重复的问题，不代表已验证所有 UTM 或 Apple 后端组合。

日志保存在安装环境的 `/run/mnos/Xorg.normal.log`、`Xorg.compat.log` 和
`journalctl -u mnos-installer`。串口会记录 `MNOS GRAPHICS` 的选用模式或失败提示。

官方说明：[QEMU 的 Use Hypervisor](https://docs.getutm.app/settings-qemu/qemu/#use-hypervisor)、
[Apple 后端](https://docs.getutm.app/settings-apple/settings-apple/)、
[虚拟显卡与 VirGL](https://docs.getutm.app/settings-qemu/devices/display/)。
