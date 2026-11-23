# mnOS 两个版本

| | 自研版 Native Edition | Linux Edition |
| --- | --- | --- |
| 宿主内核 | 原创 C／汇编 mnOS 内核 | Debian Linux |
| 桌面 | 原生窗口、应用菜单、文件、编辑器、终端和设置 | Xfce，加 mnOS C/C++ 管理工具 |
| 安装 | UEFI、MNFS；AMD64／ARM64 各一份 ISO | 离线 Linux 安装器、ext4；AMD64／ARM64 各一份 ISO |
| Linux 应用 | 默认关闭的 Debian riscv64 软件模拟环境 | 对应本机架构的 Linux 应用 |
| 软件包 | 客体内使用 APT、riscv64/all DEB | APT、AMD64／ARM64 DEB |

自研版的宿主不是 Linux。可选的 Linux 环境含真实 Linux 内核和 Debian 软件，由
自写的 RV64GC/Sv39 指令解释器执行，支持 X11 帧缓冲图形程序、键鼠、VirtIO 网络
和私有持久磁盘。这是当前源码的实现；旧的 USB 驱动预览 ISO 不会自动获得这些功能。
请查看具体发布版本的清单和[构建、使用及验证说明](native/guest/README.md)。

在原生终端执行 `linux open applications`，确认后进入可搜索的应用中心；
`linux open desktop` 打开图形终端，`linux start`
打开行式控制台。F5 返回宿主。`linux apps` 列入口，`linux status` 查状态与内存。
在 Linux 环境中用 `apt update`、`apt install 包名`、`apt list --installed`。
`mnos-gui list` 发现已安装的桌面应用；`mnos-gui open 桌面ID` 启动应用，
`mnos-gui run 程序 [参数]` 启动没有桌面条目的程序。当前源码集成 Firefox ESR、
Thunar 和 Mousepad；发布这些二进制前，必须通过自研宿主内的真实窗口测试。
nano 等全屏程序在图形终端 Xterm 中运行。`help` 默认英文，`help -cn` 中文。

安装后的系统把当前用户的软件和文件保存在个人目录下的 8 GiB 稀疏数据文件。
请先在客体执行 `poweroff`；直接关闭相当于断电，未同步的内容可能丢失。
`linux stop` 擦除内存但保留磁盘；`linux pause`／`linux resume` 保留运行状态。
关窗口不停止后台任务，退出登录会关闭客体。不共享宿主目录或原始磁盘；客体 root
不等于宿主 root。解释器目前仍是有边界检查的宿主内核组件，尚未隔离成独立用户进程，
没有安全认证。

目前只有一个软件解释 CPU，没有硬件虚拟化、向量指令或 3D 加速。停止时可以用
`linux memory MIB`／`linux memory auto` 设置内存上限，实际受宿主空闲内存约束。
APT/X11 建议至少 768 MiB 客体内存、4 GiB 宿主内存。解释执行存在明显性能开销，
当前版本尚不能作为高性能生产服务器发行版。

DEB 不能靠解包自动变换 CPU 架构：官方 AMD64／ARM64 VS Code 包不能直接在
riscv64 客体运行。独立 Linux 版已包含对应架构的官方 VS Code、Firefox 和更完整的
CTF 工具；未逐个验证的程序不会宣称已兼容自研版。mnOS 新代码使用 C/C++、汇编
和 shell；第三方工具必要的 Python 依赖按需保留。

兼容范围以发布记录为准。ARM64 QEMU virt/UEFI 不等于 Apple 虚拟化框架或全部
ARM 开发板。原生内核的物理 Wi-Fi、网卡和 GPU 支持仍少于 Linux。

Linux 版通过管理中心和 `mnos-admin help`／`help -cn` 管理服务、账号、磁盘、网络、
日志和更新；查询遵守普通账号权限，需 root 的修改明确认证。自研版的 `system`、
`kernel` 和设置调用实际宿主服务。`support` 提供 report.mnos@proton.me 和
wrmfwv@163.com，系统不会自动发送反馈。
