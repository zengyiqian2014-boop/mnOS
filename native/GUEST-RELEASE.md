mnOS Native Edition 0.5 Preview：AMD64、ARM64 各一份原生图形系统安装 ISO。宿主运行本项目的 C／汇编内核和桌面；内置新写的 C 软件模拟器，在桌面窗口中启动真正的 Linux 客体。

- GUI 包含应用菜单、文件管理器、编辑器、终端、监视器、设备和设置，以及新增的 **Linux environment** 窗口。安装完成后移除 ISO，从硬盘进入系统。
- 新增 `system` 系统查询入口和 `linux start|status|pause|resume|stop|restart` 客体管理。`help` 默认英文，`help -cn` 中文；命令手册说明参数、权限和限制。
- 客体运行 Linux 6.12.114、musl 1.2.5、BusyBox 1.37.0，编入 402 个 applet。实际测试覆盖 shell、管道、SHA256、awk、文件读写、暂停恢复、停止与重启后的 RAM 清除。编入命令不代表其依赖的设备或服务均已实现。
- 安装与运行检查覆盖两种架构的真实图形安装、移除 ISO 后磁盘启动、原生应用、账号权限、文件和设置持久化，再进入 Linux 客体执行程序。校验和、对应源代码、上游源代码包、构建配置和截图日志随本页提供。

这是开发预览版。客体是 RV64IMAC/LP64、64 MiB RAM、一个软件 CPU，支持 UTF-8 行式控制台；暂无浮点／向量指令、完整 ANSI 终端、网卡、GPU 或宿主磁盘／文件共享。客体文件在停止、重启或宿主切换账号后丢失。模拟器仍在宿主内核内运行，没有经过独立安全认证。Firefox、VS Code 和完整 GUI／CTF 工具请使用另行发布的 **mnOS Linux Edition**；本页原生版不能运行这些 Linux 桌面应用。

使用 UEFI 启动。建议 1 GiB 内存和至少 1 GiB 空白磁盘，安装器会要求明确确认清空所选磁盘。已测设备：AMD64 QEMU PC／OVMF／IDE／标准 VGA／PS/2；ARM64 QEMU virt／UEFI／VirtIO MMIO block／ramfb／VirtIO MMIO 输入。UTM 使用 QEMU 后端；未验证 Apple Virtualization.framework 或全部真实硬件。

安装后打开 **Applications → Linux environment → Start**，或在原生终端输入 `linux start`。客体内用 `busybox --list`、`COMMAND --help`、`help -cn` 查命令；F5 返回宿主终端。详细区别见仓库 [EDITIONS.md](https://github.com/zengyiqian2014-boop/mnOS/blob/main/EDITIONS.md)。
