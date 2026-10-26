# mnOS 两个版本

| | Native Edition 自研版 | Linux Edition |
| --- | --- | --- |
| 启动后运行的宿主内核 | 原生 mnOS C／汇编内核 | Debian 维护的 Linux 内核 |
| 图形桌面 | 自写窗口、应用菜单、文件、编辑器、终端、监视器和设置 | Xfce，加上 mnOS 的 C/C++ 工具与管理中心 |
| 安装 | 图形 UEFI 安装器、MNFS，AMD64／ARM64 分别提供 ISO | 离线安装器、ext4，AMD64／ARM64 分别提供 ISO |
| Linux 应用 | 内置软件模拟器中的 RV64IMAC/LP64 Linux 控制台程序 | 本机架构的 Linux 程序、Firefox、VS Code、CTF 工具 |
| 管理入口 | Settings、Monitor、`system`、`kernel`、`linux` | 系统管理中心、`mnos-admin`、`kernel` 及上游系统管理器 |

自研版的宿主确实运行本项目的内核。Linux 窗口内有另一套真实的 Linux 内核和用户程序，
由自写的 C 指令解释器、Sv39 内存转换和 SBI 接口运行。启动客体无需重启或切换宿主，
也不会把命令转交给隐藏的 Linux 宿主。客体中的 Linux、musl、BusyBox 和编译器运行库
是保留各自许可证的上游软件；没有将它们冒称为自研内核。

## 自研版：Linux 客体怎么用

安装后，在左上角 **Applications → Linux environment** 点 **Start**，或者在原生
Terminal 中输入 `linux start`。客体提示符是 `linux-guest#`。

```sh
uname -a                          # 查看实际运行的客体 Linux
busybox --list                     # 查看编入的 402 个 applet
help                              # 英文帮助
help -cn                          # 中文帮助
ls --help                         # 具体命令的语法
printf abc | sha256sum             # 管道与校验和
echo hello > /tmp/note
cat /tmp/note                      # 真实客体文件读写
awk 'BEGIN { print 6*7 }'
```

F5 回到宿主终端，`linux pause`／`linux resume` 暂停和恢复，`linux status` 查状态，
`linux stop` 停止，`linux restart` 清空后重启。关闭窗口只隐藏界面。
`system status` 汇总宿主信息；`system cpu|memory|processes|storage|security|display|guest`
按对象查询。原生命令也有 `help` 和 `help -cn`，与客体命令各自独立。

初期客体分配 64 MiB RAM、一个软件 CPU。支持 RV64IMAC 指令与 LP64 ABI，
没有浮点／向量 ISA、虚拟网卡、GPU、宿主文件、磁盘或设备共享。
不能直接运行 AMD64／ARM64 Linux 二进制；Firefox、VS Code 和图形 Linux 应用尚不支持。
窗口是 UTF-8 行式控制台（`TERM=dumb`），尚未实现完整 ANSI 光标控制和全屏编辑器。
可使用行式程序或 `top -b -n 1` 这类批量输出；编辑原生文件可用宿主 Text editor。
命令编入清单不等于相关设备、服务或所有操作都已实现。

客体文件仅在 RAM 中，停止、重启或宿主切换账号后会丢弃。客体 root 没有宿主文件或
设备接口。模拟器当前仍运行在宿主内核中，还不是独立用户态进程，没有经过独立安全认证。

## Linux 版：完整应用与系统管理

一次离线安装包含桌面、Firefox ESR、官方 VS Code、nano、C/C++ 编译环境、系统工具、
CTF 工具和相应依赖。自写管理程序采用 C/C++；Python 仅作为第三方工具的必要依赖。

左上角工具中心提供应用搜索、CTF 分类、实际安装的 Linux 命令目录及系统设置。
**mnOS System Management** 或 `mnos-admin gui` 打开集中管理入口，覆盖服务、账户、
磁盘、网络、日志、更新、进程、显示和内核设置。系统管理器及桌面仍明确标注其上游来源。

```sh
mnos-admin help                    # 英文管理手册
mnos-admin help -cn                # 中文管理手册
mnos-admin info
mnos-admin processes
mnos-admin storage
mnos-admin network
mnos-admin accounts
mnos-admin services list
mnos-admin services status NetworkManager.service
mnos-admin logs NetworkManager.service
mnos-admin commands                # 命令图形目录
mnos-admin ctf                     # CTF 工作台
mnos-center --list-commands | less # 实际安装命令及路径
help -cn management
help -cn ctf
```

服务修改使用 `sudo mnos-admin services start|stop|restart|enable|disable UNIT`，
必须给出明确的服务单元。查询按当前账户权限执行，受限日志仍需权限。图形管理器在应用
系统修改时正常认证，打开工具目录不会自动扫描网络或授予管理员身份。
软件可用 `sudo mnos-pkg install 包名` 安装，保留上游签名和依赖检查；新增软件需联网
或备齐离线依赖。Linux 没有固定的“所有命令”全集，以本机实际安装清单为准。

## 安装、兼容范围与验证

按宿主架构选 AMD64 或 ARM64 镜像，两个版本分别安装。安装会清空明确选择并再次确认的
目标盘；确认前应保存需要的资料。安装后移除 ISO，从硬盘启动。

两版均按实际固件和驱动支持范围工作。原生驱动范围小于 Linux：AMD64 原生安装测试使用
QEMU PC／OVMF／IDE／VGA／PS/2；ARM64 原生测试使用 QEMU virt／UEFI／VirtIO MMIO
block／ramfb／VirtIO MMIO 键鼠。UTM 使用 QEMU 后端；Apple Virtualization.framework、
任意 Apple Silicon 裸机及全部真实硬件均未宣称支持。

原生测试实际完成两架构安装、拔掉 ISO 后 GUI 磁盘启动、账号及文件权限、文件和设置
持久化，并在客体运行 Linux 命令、管道、校验和及文件读写。内存边界、非法指令、
Sv39 权限等另有 ASan/UBSan 测试。Linux 版检查离线工具、图形窗口、安装与磁盘启动、
账户认证、Firefox 渲染和 VS Code 实际编辑保存。对应测试记录、源码版本、校验和随发布提供。
这些是开发预览版的具体功能测试，不代表全硬件兼容、安全认证或性能优于 Linux/RHEL。

构建步骤和源代码许可见 [英文说明](EDITIONS.md)及[客体构建说明](native/guest/README.md)。
