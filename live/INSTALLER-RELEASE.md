mnOS 集成工具安装包 Preview 3：Firefox ESR、官方 VS Code、nano、开发编译环境、CTF 与系统工具、驱动固件以及最新自研 C/汇编内核，都放在同一架构的一份 ISO 内。安装器一次写入全部组件，浏览器和编辑器无需另行下载安装。

- **AMD64**：Intel/AMD 64 位；**ARM64**：AArch64 UEFI。两种镜像分别匹配 CPU 架构，同一架构无需再分虚拟机版和硬件版。
- 图形安装器先选择磁盘、输入账号、选择网络工具授权模式，确认清空整盘后安装。完成后移除 ISO，从硬盘启动。尚无保留旧系统、手动分区或安装时磁盘加密。
- 新增 C 编写的 `mnos-pkg`：普通用户可以 `list/search/show/check`；修改软件需要显式执行 `sudo mnos-pkg update/install/remove/upgrade`。保留签名校验、依赖解析和 APT 确认，不自动提权或执行下载的安装脚本。
- `help` 默认英文，`help -cn` 中文；`help -cn code`、`firefox`、`packages`、`build`、`network`、`kernel` 等章节解释用途、参数、权限、文件位置、示例和错误排查。`help 工具名` 阅读随包安装的上游手册，没有手册时打开该工具内置帮助；语言取决于上游。恢复基础镜像曾过滤掉的离线手册。
- 移除主动安装的 Java SDK 和菜单项，Python 只为选中的第三方工具保留必要依赖。补齐 C/C++ 标准头文件、构建工具和开发手册；新命令和帮助使用 C，界面使用 C++。
- 更新自研内核字库、真实字距和原创图标；49,776 个 Unicode 码位、UTF-8 1–4 字节、统一 `kernel 指令 [对象]`、Tab 补全、中英文逐条帮助和输出翻阅。原生功能及验证单独见 `native/DESKTOP-RELEASE.md`。

**Firefox、VS Code 运行在 Debian 13/Linux/Xfce 工具环境。自研内核与原生桌面是另一个引导入口，目前没有 Linux 应用兼容层。** 原生入口不是 Linux 换名；工具环境也不是自研内核。只有检测到受支持的 MNFS 卷时原生账号与文件才持久化；Linux 的 ext4 分区不属于 MNFS。原生专用桌面 ISO 提供 MNFS 安装流程，但不含 Linux 应用。

这仍是开发预览版。相应发行产物已通过 AMD64/ARM64 实际图形安装、移除 ISO 启动、账号认证、VirtIO/NVMe 磁盘、AMD64 BIOS 及应用环境检查。应用补充测试启动未改动的同一 ISO，以普通账号打开 VS Code，等待源码正文显示，再通过虚拟 USB 键盘编辑、保存，并从文件系统核对内容；AMD64 使用 KVM，ARM64 使用 TCG 软件模拟。Firefox 已通过无网络的无头页面渲染检查，C/C++ 编译、离线手册与软件管理参数/权限检查也已通过。日志、截图、包清单、内核配置、SHA256 和源码提交号随发布提供。

UTM 推荐 QEMU 后端，取消“使用 Apple 虚拟化”；同架构来宾仍可使用 QEMU/HVF CPU 加速。没有 macOS 实机验证，不声称 Apple Virtualization.framework 已修复。实体硬件及所有型号也未逐一实测。原生入口需关闭 Secure Boot，Linux 入口保留 Debian 签名链。账号底层组件、原生应用 ABI、硬件驱动等实际限制见随镜像文档，不宣称无限长度或全面兼容。
