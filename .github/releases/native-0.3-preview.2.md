本版运行 mnOS 自研 C／汇编内核，提供 AMD64 和 ARM64 两个原生启动 ISO。它是原生系统开发预览，尚不是完整桌面安装器。

在 Preview 1 的隔离多进程基础上，新增原生程序文件 API：创建、二进制读写、查询、截断、删除、创建目录，以及查询当前用户的主目录。普通用户可管理自己的文件，应用不需要 root；跨用户目录、账户记录、非法内存地址和对代码页的写入会被拒绝。磁盘锁定和 I/O 失败向程序返回明确错误。实现和新增测试使用 C／C++／汇编／Shell。

每次读写最多 4 KiB，支持 64 位文件偏移。实际原生 ELF 在两种架构上写入并读回 32 TiB 以上的稀疏文件数据；第二次启动验证磁盘上的字节保持一致。ASan/UBSan 验证实际 MNFS、账户权限和用户内存边界。多进程回归保留 512 进程、内存故障回滚、1000 次回收循环，以及 QEMU 中的抢占、休眠、异常隔离和账号切换测试。

- `mnOS-native-amd64.iso`：AMD64 / x86-64。
- `mnOS-native-arm64.iso`：ARM64 / AArch64。
- `SHA256SUMS`、`SOURCE_COMMIT.txt`、`PROVENANCE.txt`：文件校验、源码和成功 CI 产物身份。
- `boot-validation.tar.gz`：主机回归及实际启动日志。

下载镜像是成功 CI 中实际测试过的原始文件。`help` 默认英文，`help -cn` 显示中文。`exec PATH`、`ps`、`kill PID` 管理原生进程；文件 ABI 与 C 示例见 `native/user/api.h` 和 `native/user/example.c`。

当前文件系统为自研 MNFS，APFS 尚未实现。原生桌面、服务、驱动和安装器的后续范围见 [原生系统路线](https://github.com/zengyiqian2014-boop/mnOS/blob/main/native/SYSTEM.md)。当前应用仍使用小型静态 ABI，在启动 CPU 上调度；文件 I/O 为同步调用。尚无 Linux/POSIX 应用兼容或全硬件兼容保证。x86 仍使用 GRUB，字体来自 Noto，授权说明保留。QEMU TCG 测试不能代替 Apple Virtualization.framework 或实体设备验证。

旧的 `v2.0-installer-preview.2` 默认仍是 Debian/Linux/Xfce，未被此发布替换。Firefox、VS Code 和 CTF 工具尚未迁移到原生系统。
