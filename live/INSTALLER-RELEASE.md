**只需下载与你的虚拟机／电脑 CPU 架构对应的一份 ISO，安装一次。**
桌面、Firefox、VS Code、nano、开发与安全工具、驱动，以及对应架构的实验内核都已包含在其中，安装时一起写入硬盘，无需联网下载或逐个安装组件。

| CPU 架构 | 完整离线安装包 |
| --- | --- |
| ARM64 / AArch64 | [mnOS-2.0-installer-preview.2-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v2.0-installer-preview.2/mnOS-2.0-installer-preview.2-arm64.iso) |
| Intel / AMD 64 位 | [mnOS-2.0-installer-preview.2-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v2.0-installer-preview.2/mnOS-2.0-installer-preview.2-amd64.iso) |

两份镜像的区别是 CPU 架构；同一架构镜像同时包含虚拟机驱动、来宾工具和常见实体硬件固件。
发布页的 `.sha256`、`.tsv`、`.config`、`.txt` 和验证压缩包是校验及开发资料，不是另外需要安装的软件包。

启动后选择第一项 **`Install mnOS - arm64 (virtual machines and hardware)`**（amd64 镜像对应显示 amd64），即可安装全部组件。
软件渲染、临时桌面和实验内核入口是可选启动方式，无需逐项执行来完成安装。
若看到 `mnOS Desktop 2.0 - hardware / safe graphics / boot messages` 三项菜单，当前显示的是旧版引导配置。请先关机，核对所有虚拟光驱，移除旧镜像，仅挂载上面的安装 ISO，再启动。
`grub> boot` 报 `you need to load the kernel first` 仅说明该命令执行时没有已加载的内核；它不能证明哪个文件缺失，也不表示需要另装一个内核包。

启动后直接进入 C++ / GTK 图形安装器，选择磁盘、创建账号、确认清盘后安装。
安装完成后关机、移除 ISO，再从硬盘启动。建议虚拟机配置 4 GiB 内存和 40 GiB 磁盘。
当前安装方式会清空整块所选磁盘，尚无保留旧系统、手动分区或磁盘加密流程。

包含 Firefox、VS Code、nano、C/C++、Rust、Go、Java、Node.js、CTF 与系统管理工具。
网络工具权限可选择 sudo 或向安装账号委派 NET_ADMIN / NET_RAW。root 登录保持锁定。
移除安装器自行设置的用户名 31 字符上限，以及密码 12–512 字符／强度拦截。输入系统已占用的账号或组名时显示“此用户名已被系统占用，请填写一个其他的”。真实账号组件和密码哈希会在确认清盘前预检，后端写盘前再次校验；不会截断或自动改名。当前底层组件仍有用户名 255 字节、非空密码 511 字节的实际边界，尚未实现无限长度认证。
`help` 默认英文，`help -cn` 为中文。新增安装器和测试使用 C++、Shell；第三方工具需要的 Python 依赖仍保留。

终端和串口登录改为由系统 `login` / PAM 直接收集用户名，修复 agetty 额外的 42 字符限制导致长账号被退回登录界面的问题。密码认证、普通用户权限和 root 锁定保持有效；未启用自动登录。验证包含错误密码被拒绝，以及长用户名的真实图形／终端登录。

修复上一版第二项与第一项显示路径相同的问题。第二项现在使用 `mnos.graphics=compat`，显式关闭 Xorg glamor/GLX、硬件光标和翻页，同时保留 VirtIO/KMS 显示驱动。第一项的 Xorg 会话退出失败时，也会自动重试兼容配置。失败日志保存在 `/run/mnos/Xorg.*.log` 及系统日志中，串口输出所选模式；连续失败有重试上限。
安装会话关闭屏保和显示节能，避免较长安装期间虚拟机显示“Display output is not active”；测试会持续检查安装画面。
修复自研 x86 内核仅依赖 RDRAND 导致虚拟机无法创建账号的问题：新增现代/传统 VirtIO PCI 随机数驱动；ARM64 增加 RNDR 支持。完全缺少安全随机数来源时仍拒绝创建密码，并给出设备提示。
ARM64 使用 UEFI，amd64 支持 UEFI/BIOS。自动测试使用 Linux 上的 QEMU KVM/TCG。

**UTM：“使用 Apple 虚拟化”与 QEMU 的“Use Hypervisor”是不同后端／设置。** 先选择“虚拟化 → Linux”，取消“使用 Apple 虚拟化”；在 QEMU 设置保留 Use Hypervisor，Apple Silicon 主机配 ARM64 来宾仍可使用 HVF CPU 硬件加速。显卡先选普通 VirtIO，关闭 GL／VirGL。详见 [UTM 设置说明](https://github.com/zengyiqian2014-boop/mnOS/blob/main/live/UTM.md)。本次没有 macOS 实机执行条件，尚不能声明 Apple Virtualization.framework 或 UTM/HVF 黑屏已经修复；兼容显示修订解决的是已确认的重复回退入口问题。

用户已提供截图确认：关闭“使用 Apple 虚拟化”后，同一 ARM64 ISO 已进入图形安装器，并识别到 64 GiB 的 VirtIO 磁盘。用户侧安装完成及硬盘启动结果仍待确认；没有据此宣布 Apple Virtualization.framework 兼容问题已解决。

发布前测试从实际 ISO 的图形界面安装到空白虚拟磁盘，再移除 ISO、重置 UEFI 变量，从已安装磁盘启动。
检查普通账号登录、图形桌面、软件入口和 VirtIO / NVMe 控制器切换；amd64 另检查 BIOS 启动。
另以关闭 RDRAND 的虚拟 CPU 实测两种 VirtIO 随机数设备下的账号创建，并验证 ARM64 RNDR 和缺少随机数来源时的拒绝路径。
验证日志和截图见 `installation-validation.tar.gz`，各 ISO 附 SHA256、包清单、内核配置及源码提交号。
`PAYLOAD_COMMIT.txt` 记录基础系统来源，`SOURCE_COMMIT.txt` 记录发布源码。本次在已构建的基础系统上加入安装器修订层，并重新测试最终 ISO；安装器会将终端登录修订一并部署到硬盘。基础系统及修订层均纳入镜像内部校验清单。下载文件名包含 `.2`，可在虚拟光驱设置里与旧版区分。

桌面和应用运行在 Debian 13 / Linux 6.12 LTS 的 Xfce 环境。安装器、工具菜单和系统中心由 mnOS 提供，Xfce 本身是第三方桌面组件。
两个架构均附实验性的原生 C/汇编内核及源码；原生入口需关闭 Secure Boot，ARM64 原生图形依赖 GOP，尚无 Linux 应用兼容层。
实体硬件、UTM/Parallels/VMware 的所有型号、Apple Silicon 裸机及各 ARM 开发板未逐一实测，不将 QEMU 测试等同于全部硬件兼容。
