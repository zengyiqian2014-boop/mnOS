这是 mnOS 原生内核的开发预览版，包含 amd64 和 ARM64 两个启动 ISO。两者运行 mnOS 自己的 C/汇编内核，不依赖 Linux 内核。它们是原生内核测试介质，尚不是带 Firefox、VS Code 和完整桌面的安装系统。

本次加入动态多进程：每个应用有独立页表、数据和栈，支持定时抢占、休眠、进程身份和退出回收。一个应用的非法指针、代码写入或未支持的寄存器指令会终止该应用。`exec PATH` 启动原生 ELF，`ps` 查看进程，`kill PID` 结束自己拥有的进程；root 可管理其他进程。`help` 显示英文帮助，`help -cn` 显示中文帮助。

发布文件直接取自提交 `5fc4544e28a74442ddc2b9437608adf97e053c9e` 的成功 CI 产物，不重新编译。下载时选择处理器架构：

- `mnOS-native-amd64.iso`：x86-64 / AMD64。
- `mnOS-native-arm64.iso`：ARM64 / AArch64。
- `SHA256SUMS`：下载校验值；`SOURCE_COMMIT.txt`、`PROVENANCE.txt`：源码和 CI 产物信息。
- `boot-validation.tar.gz`：实际启动和回归测试日志。

验证包括 ASan/UBSan 下的 512 个并发进程、全部八处分配失败回滚、内存压力及 1000 次创建/销毁循环；QEMU 中的双架构真实启动、16 个持续计算应用和 4 个休眠应用、异常隔离、账号切换回收，以及第二次启动后的 MNFS 文件/账号持久化。另有 x86 四核启动和 ARM FDT/ACPI、GIC、多核启动组合。测试使用 QEMU TCG；未验证 Apple Virtualization.framework 或所有实体设备。

当前限制：应用在启动 CPU 上调度；原生 ELF 只有小型静态整数 ABI，尚无 POSIX/Linux 应用兼容、用户态文件 API、完整图形桌面和安装器。现有自研 MNFS 支持 64 位偏移、CoW、校验和及快照；APFS 尚未实现。x86 启动介质使用 GRUB，字体来自 Noto，授权信息保留在源码中。此版本不作“安全认证”“性能超越 RHEL”或“兼容所有硬件”的宣称。

验证记录：
- https://github.com/zengyiqian2014-boop/mnOS/actions/runs/39185118793
- https://github.com/zengyiqian2014-boop/mnOS/actions/runs/39185118817
