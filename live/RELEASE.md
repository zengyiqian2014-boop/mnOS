mnOS 双启动预览版：同一张 ISO 提供自研 C/汇编内核和独立的 Linux 桌面工具环境。下载 `qemu` 版用于虚拟机，`hardware` 版用于 x86_64 PC；后者增加常见 Wi-Fi、GPU、蓝牙和音频固件及 CPU 微码。

### 编辑文件与编程

- 加入 **Microsoft 官方 Visual Studio Code**，可从应用菜单或系统中心打开，也可用 `code .` 打开项目。默认关闭遥测与内置 AI 功能；正常以普通用户运行，保留 Electron 沙箱。
- 提供 `nano`、Mousepad 图形编辑器、Thunar 文件管理器；编辑个人文件、运行桌面应用和编译程序不需要 root。
- 预装 C/C++、Clang/clangd、GDB、Rust/Cargo、Go、Java JDK、Node.js/npm、Python/venv/pytest，以及 Git、CMake、Ninja、pkg-config、ripgrep 和 ShellCheck。
- 保留 Firefox ESR、Pwntools、Wireshark、Nmap、Hashcat 等网络、CTF 和系统管理工具。

### 账号与权限修正

- **自研内核加入本地账号认证**：首次设置普通用户和独立 root 密码，提供登录、锁定、修改密码、`su`、`exit` 和 root 创建账号。MNFS 磁盘保存加盐 PBKDF2-HMAC-SHA256 密码记录，失败尝试有延迟，root 会话五分钟到期。
- 普通用户可查询状态、读写自己的文件、创建和整理个人目录、修改并保存自己的分辨率/壁纸。其他用户目录、root 目录和内部密码记录受到访问检查；整卷操作和账号管理需要 root。
- Linux `/usr/bin/kernel` 提供普通用户查询；修改内核状态由 `/usr/sbin/mnos-kernel-admin` 再次检查身份。日常文件操作和开发工具不要求 root；模块装卸、修改 sysctl、CPU 上下线及 TuneD 配置需要管理员认证。
- 安装器提供网络权限选项：默认特权网络操作使用 root/sudo；选择开放模式时，仅给所选普通账号授权组权限，通过 `mnos-net nmap …`、`mnos-net tcpdump …`、`mnos-net ip …`、`tc`、`iw` 使用 **NET_ADMIN + NET_RAW**，账号 UID 仍为普通用户。网络工具仍全部预装；无特权的连接扫描等不受此选项限制。
- 开放模式允许受授权工具及其子进程修改网络、抓包和发送原始报文。关闭授权后，已经运行的工具需退出才能失去已获取的权限。没有添加通用免密 sudo。

### 帮助命令

**`help` 默认英文，`help -cn` 是中文。** 两个环境都支持：

```text
help
help -cn
kernel help
kernel help -cn
```

内核命令格式为 `kernel <指令> [对象]`。Linux 查询示例：`kernel info`、`kernel memory`、`kernel gpu`；管理员操作示例：`sudo kernel performance balanced`。`help cd` 等 Bash 内建命令帮助保留。

### 已有内核与硬件能力

自研内核保留稀疏物理页管理、最高 64 TiB 物理地址范围（还受 CPU 地址宽度限制）、动态多核发现、每核抢占调度和原创 MNFS。已验证 33 TiB 稀疏 QEMU 地址读写及最多 32 核配置；这不是实体内存压力测试或无限核数保证。MNFS 支持超过 4 GiB 的稀疏文件、CoW、CRC32C、目录、重命名、截断及一个快照，采用独立格式，不兼容 APFS。

原生 F7 设置支持 QEMU/Bochs 2D 分辨率与壁纸；其他显卡暂用启动帧缓冲。Linux 提供完整桌面设置、账号/服务/软件管理以及 Mesa OpenGL/Vulkan 工具。**VS Code、Firefox 和完整开发环境运行在 Linux 入口；自研内核尚无 Linux/POSIX 应用 ABI。**

### 下载与验证

每个 ISO 随附 `.sha256`、软件包清单、Linux 内核配置和源码提交号。两版支持 BIOS/UEFI；**自研内核需要关闭 Secure Boot**，Linux 入口保留 Debian 签名链。

Linux 第一次启动创建本地管理员，无默认密码。Live 文件、账号和设置在关机后丢失，长期使用需安装或另行保存。原生没有 MNFS 磁盘时账号仅本次启动有效；有磁盘时，登录后可在个人目录持久写入。两套环境的账号和存储相互独立。

发布流程检查原生密码算法、权限与文件持久化、BIOS/UEFI、SMP/隔离、33 TiB 地址、文件系统故障注入、双语帮助和显示设置，并检查 Linux 普通用户/管理权限、网络授权及撤销、多语言程序编译运行、桌面组件和两种 ISO 的 BIOS/UEFI/Secure Boot 启动。详细结果见 `live/VALIDATION.md` 与构建 artifacts。

自研认证目前限本地控制台和内核界面，未实现 POSIX 进程身份/磁盘 ACL、原生 Wi-Fi/USB/NVMe 或 GPU 3D 加速。未签名的自研内核和未加密的 MNFS 不防范离线篡改。实体 PC、无线连接、实体显卡加速和安装器完整写盘流程尚未验证；不宣称通过安全认证或性能超过 RHEL/Linux。

源码与说明见仓库 `native/README.md`、`native/FILESYSTEM.md` 和 `live/README.md`；ISO 内原生源码位于 `/usr/src/mnos-native`。
