mnOS 双启动预览版：同一张 ISO 提供自研 C/汇编内核和独立的 Linux 工具环境。下载 `qemu` 版用于虚拟机，`hardware` 版用于 x86_64 PC；后者增加常见 Wi-Fi、GPU、蓝牙和音频固件及 CPU 微码。

### 帮助命令

**`help` 默认英文，`help -cn` 是中文。** 两个环境都支持：

```text
help
help -cn
kernel help
kernel help -cn
```

内核命令格式为 `kernel <指令> [对象]`。Linux 中除帮助外需要 root，例如 `sudo kernel info`、`sudo kernel memory`、`sudo kernel gpu`。`help cd` 等 Bash 内建命令帮助保留。

### 本次更新

- 自研内核采用稀疏物理页管理，物理地址范围最高 64 TiB，另受 CPU 地址宽度限制。已完成 33 TiB 稀疏 QEMU 内存启动及高地址读写验证；不是 33 TiB 实体内存压力测试。
- 动态多核发现、xAPIC/x2APIC、每核独立抢占调度和隔离检查。没有固定 CPU 数组上限；已测 4、8、32 核，以及 APIC ID 为 0/256 的双核配置。实际规模仍受硬件、内存与测试范围约束。
- 原创 MNFS 文件系统：64 位稀疏文件、CoW、CRC32C、目录、重命名、截断和一个保存快照。可在 MNFS ATA 镜像上持久保存文件；支持超过 4 GiB 的文件。采用独立格式，不兼容 APFS。Linux 根文件系统继续使用成熟的 Linux 文件系统。
- 原生 F7 显示设置：QEMU/Bochs 2D 分辨率切换、三种壁纸，设置可保存到 MNFS。其他显卡暂时使用启动帧缓冲。Linux 提供分辨率、壁纸、多屏、声音、网络、账号等常规设置，以及 Mesa OpenGL/Vulkan 工具。
- Linux `kernel` 管理工具：内核信息、CPU/内存、显卡驱动、模块、日志、sysctl、支持热插拔的 CPU 上下线和 TuneD 配置。普通用户不能执行管理操作，帮助无需提权。
- 保留 Firefox ESR、Python、Pwntools、GDB、Wireshark、Nmap、Hashcat 等 CTF/系统工具。更新项目介绍、系统内“关于”和本地手册。

### 下载与启动

每个 ISO 随附 `.sha256`、软件包清单、Linux 内核配置和源码提交号。请核对所下载 ISO 的校验文件。两版均支持 BIOS/UEFI；**自研内核需要关闭 Secure Boot**，Linux 入口保留 Debian 签名链。

Linux 第一次启动创建本地管理员，没有默认密码。Live 会话中的文件、账号和设置在关机后丢失；长期使用需安装到磁盘或另行保存数据。原生 `/ram` 同样临时，挂载的 MNFS `/disk` 才持久，默认只读，`disk unlock` 后可写入。

### 验证范围

发布流程执行原生 BIOS/UEFI、每核隔离、33 TiB 地址验证、文件系统故障注入、ATA 断电重启、双语帮助、QEMU 显示设置，以及 Linux 工具/权限检查、BIOS/UEFI/Secure Boot 和完整 ISO 的原生启动入口检查。日志保存在构建 artifacts，部分原生记录随版本附带。

原生内核仍为实验版本，尚无完整 Linux/POSIX ABI、原生 Wi-Fi/USB/NVMe 或 GPU 3D 加速。实体 PC、无线连接、实体显卡加速和安装器完整安装流程未在本环境验证；包含驱动不等于兼容所有硬件。本版不宣称通过安全认证，也不宣称性能已超过 RHEL/Linux。

详见仓库 `native/README.md`、`native/FILESYSTEM.md`、`live/README.md` 和 `live/VALIDATION.md`；ISO 内原生源码位于 `/usr/src/mnos-native`。
