mnOS Native Edition 0.5 Preview 2：AMD64、ARM64 各一份原生图形系统安装 ISO。宿主运行本项目的 C／汇编内核和桌面；内置 C 软件模拟器，在桌面窗口中启动真正的 Linux 客体。

- **Linux 模拟默认关闭。** 打开 Linux 程序时弹出「是否开启 Linux 模拟？」；Enter 开启并打开，Esc 取消。取消不会启动客体。已开启时打开其他受支持程序无需重复确认。
- F7 **系统设置 → Linux 模拟 / L** 随时关闭，立即停止并擦除客体 RAM、输入队列和临时文件。注销、切换账号或重启后默认关闭；关闭窗口仅隐藏窗口。
- 修正 AMD64 在 Linux 模拟忙碌时键鼠输入可能被延迟的问题；定时器处理会先检查待处理输入，使切换窗口和关闭模拟仍可操作。
- 修正 AMD64 键盘的冒号与双引号映射；编辑器的输入和光标操作使用真实键盘事件进行回归检查。
- `linux apps` 列出终端、文件列表、进程、系统信息和命令目录入口，`linux open APP` 在客体就绪后运行所选命令行程序。`linux start|status|pause|resume|stop|restart` 继续可用，另有 `enable`／`disable`。`help` 默认英文，`help -cn` 中文；详细帮助和 Tab 补全同步更新。
- GUI 包含应用菜单、文件管理器、编辑器、终端、监视器、设备、设置和 **Linux 模拟** 窗口。安装完成后移除 ISO，从硬盘进入系统。
- 客体运行 Linux 6.12.114、musl 1.2.5、BusyBox 1.37.0，编入 402 个 applet。实际测试覆盖 shell、管道、SHA256、awk、文件读写、暂停恢复、停止与重启后的 RAM 清除。编入命令不代表其依赖的设备或服务均已实现。
- 安装与运行检查覆盖两种架构的真实图形安装、移除 ISO 后磁盘启动、原生应用、账号权限、文件和设置持久化，再进入 Linux 客体执行程序。新增鼠标确认／取消、自动运行所选程序、设置开关、重新开启后文件清除和重启默认关闭检查；ASan/UBSan 检查登录边界和失败路径。校验和、对应源代码、上游源代码包、构建配置和截图日志随本页提供。

这是开发预览版。客体是 RV64IMAC/LP64、64 MiB RAM、一个软件 CPU，支持 UTF-8 行式控制台；暂无浮点／向量指令、完整 ANSI 终端、网卡、GPU 或宿主磁盘／文件共享。客体文件在停止、重启或宿主切换账号后丢失。模拟器仍在宿主内核内运行，没有经过独立安全认证。Firefox、VS Code 和完整 GUI／CTF 工具请使用另行发布的 **mnOS Linux Edition**；本页原生版不能运行这些 Linux 桌面应用。

使用 UEFI 启动。建议 1 GiB 内存和至少 1 GiB 空白磁盘，安装器会要求明确确认清空所选磁盘。已测设备：AMD64 QEMU PC／OVMF／IDE／标准 VGA／PS/2；ARM64 QEMU virt／UEFI／VirtIO MMIO block／ramfb／VirtIO MMIO 输入。UTM 使用 QEMU 后端；未验证 Apple Virtualization.framework 或全部真实硬件。

安装后打开 **Applications → Linux 模拟**，或在原生终端输入 `linux open system`，确认后运行。客体内用 `busybox --list`、`COMMAND --help`、`help -cn` 查命令；F5 返回宿主终端。宿主 `help linux` 和 `help -cn linux` 分别提供英文、中文完整说明。详细区别见仓库 [EDITIONS.zh.md](https://github.com/zengyiqian2014-boop/mnOS/blob/main/EDITIONS.zh.md)。
