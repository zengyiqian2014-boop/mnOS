每个架构一份离线安装镜像：`installer-amd64.iso` 和 `installer-arm64.iso`。
同一架构镜像同时包含虚拟机驱动、来宾工具和常见实体硬件固件，不再要求选择 QEMU / hardware 两个版本。

启动后直接进入 C++ / GTK 图形安装器，选择磁盘、创建账号、确认清盘后安装。
安装完成后关机、移除 ISO，再从硬盘启动。建议虚拟机配置 4 GiB 内存和 40 GiB 磁盘。
当前安装方式会清空整块所选磁盘，尚无保留旧系统、手动分区或磁盘加密流程。

包含 Firefox、VS Code、nano、C/C++、Rust、Go、Java、Node.js、CTF 与系统管理工具。
网络工具权限可选择 sudo 或向安装账号委派 NET_ADMIN / NET_RAW。root 登录保持锁定。
`help` 默认英文，`help -cn` 为中文。新增安装器和测试使用 C++、Shell；第三方工具需要的 Python 依赖仍保留。

修复 ARM64 安全显示入口禁用 VirtIO KMS 的配置，并提前加载显示、USB 输入及常见磁盘驱动。
安装会话关闭屏保和显示节能，避免较长安装期间虚拟机显示“Display output is not active”；测试会持续检查安装画面。
修复自研 x86 内核仅依赖 RDRAND 导致虚拟机无法创建账号的问题：新增现代/传统 VirtIO PCI 随机数驱动；ARM64 增加 RNDR 支持。完全缺少安全随机数来源时仍拒绝创建密码，并给出设备提示。
同一镜像可用于软件模拟或宿主机支持的 KVM/HVF 硬件加速。ARM64 使用 UEFI，amd64 支持 UEFI/BIOS。

发布前测试从实际 ISO 的图形界面安装到空白虚拟磁盘，再移除 ISO、重置 UEFI 变量，从已安装磁盘启动。
检查普通账号登录、图形桌面、软件入口和 VirtIO / NVMe 控制器切换；amd64 另检查 BIOS 启动。
另以关闭 RDRAND 的虚拟 CPU 实测两种 VirtIO 随机数设备下的账号创建，并验证 ARM64 RNDR 和缺少随机数来源时的拒绝路径。
验证日志和截图见 `installation-validation.tar.gz`，各 ISO 附 SHA256、包清单、内核配置及源码提交号。
镜像包含一个仅用于安装会话的显示修复层；`PAYLOAD_COMMIT.txt` 记录基础系统来源，`SOURCE_COMMIT.txt` 记录最终修复和测试来源。发布的是加入修复层后重新完成安装及启动测试的镜像。

桌面和应用运行在独立的 Linux 环境。两个架构均附实验性的原生 C/汇编内核及源码；原生入口需关闭 Secure Boot，ARM64 原生图形依赖 GOP，尚无 Linux 应用兼容层。
实体硬件、UTM/Parallels/VMware 的所有型号、Apple Silicon 裸机及各 ARM 开发板未逐一实测，不将 QEMU 测试等同于全部硬件兼容。
