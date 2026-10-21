mnOS 集成安装包 Preview 4：图形工具中心、Linux 命令库、CTF 工作台，与 Firefox ESR、官方 VS Code、开发环境、自研内核入口一起装进每个架构的一份 ISO。

- **GUI**：左上角应用菜单可搜索；新增独立“工具中心”“CTF 工作台”“Linux 命令库”入口。按图标打开图形软件，命令条目打开帮助。系统、显示、分区和内核设置仍集中管理。
- **命令库**：自动读取已安装的 bin/sbin 可执行程序及链接，覆盖后续安装的工具；133 个命名条目另有中文用途和分类。列表按页显示，搜索覆盖完整清单。可用 `mnos-center --list-commands` 导出。Linux 没有固定的“所有命令”全集，页面以本机实际安装情况为准，不把不存在的命令显示成可用功能。
- **CTF**：53 个命名 CLI 条目和图形分析工具，覆盖 Web、Pwn/逆向、密码分析、流量、取证和隐写。新增 GHex、SQLite/DB Browser、Sleuth Kit、PNGCheck、ZBar、tcpflow、ngrep、fcrackzip；保留 Wireshark、GDB、Pwntools、Nmap、SQLmap、mitmproxy、Hashcat、John、YARA、Binwalk 等。
- **帮助**：`help` 默认英文，`help -cn` 中文；新增 `help commands`、`help -cn ctf`，提供分类说明、权限、离线示例与实际限制。`help 工具名` 优先打开本地手册。自动发现的程序不会被试运行；缺少手册且没有审核过的帮助入口时明确提示。
- **语言与权限**：本次界面和命令实现使用 C/C++，构建脚本使用 shell；Python 只随必要的第三方工具依赖保留，不新增 Java SDK。普通用户可使用图形应用与本地分析工具，系统级修改需要认证；抓包与网络管理按安装时的授权模式执行。

AMD64 适用于 Intel/AMD 64 位；ARM64 适用于 AArch64 UEFI。每个架构的镜像同时提供虚拟机和硬件安装入口。图形安装器要求选择目标盘并确认清空整盘，一次安装全部组件；完成后移除 ISO，从硬盘启动。暂无保留旧系统、手动分区或安装时磁盘加密。

**本页的 Firefox、VS Code、CTF 工具和 Linux 命令运行在 Debian/Linux/Xfce 工具环境。C/C++ 工具中心是 mnOS 自行实现的界面；自研 C/汇编内核和原生 GUI 是另一引导入口，目前没有 Linux 应用 ABI。** Linux 安装目标为 ext4；原生文件持久化仍需受支持的 MNFS 卷。原生专用 GUI 安装包见另一个 native desktop release。

本发布以两种架构的完整构建与验证成功为前提：实际图形安装、移除 ISO 后启动、账号认证、VirtIO/NVMe 磁盘、AMD64 BIOS、VS Code 实际编辑保存、Firefox 离线渲染、命令索引、离线手册、C/C++ 编译，以及本地 ELF/YARA/SQLite/PCAP 样例。图形工具检查以普通用户打开工具中心、GHex、DB Browser 和 Wireshark并保存真实窗口截图。发布附包清单、源码提交号、测试日志、截图和 SHA256。

这仍是开发预览版。UTM 推荐 QEMU 后端，取消“使用 Apple 虚拟化”；同架构可使用 QEMU/HVF CPU 加速。Apple Virtualization.framework 和所有实体硬件未逐一验证，不宣称全面兼容。原生入口需关闭 Secure Boot，Linux 入口保留 Debian 签名链。
