# mnOS Desktop 0.4

这是使用 mnOS 自研内核的图形系统安装版。AMD64、ARM64 各一份 ISO；安装器、
引导加载、桌面、窗口管理、编辑器、终端及账号服务均由 `native/` 中的 C／汇编实现。
启动后直接运行原生桌面。此镜像不包含 Linux、Xfce 或 Python 运行时。

## 安装

1. 下载与 CPU 架构相符的 `mnOS-0.4-desktop-installer-*.iso`，核对 `SHA256SUMS`。
2. 使用 UEFI 启动，至少 512 MiB 内存。安装盘至少 80 MiB，建议 1 GiB 以上。
3. 在图形安装器中选择磁盘，点 **Continue to install** 或按 Enter。
4. 确认所选磁盘可以清空，输入 `ERASE` 后按 Enter。取消不会写入磁盘。
5. 安装器写入并回读验证引导文件、GPT 和 MNFS。完成后关机、移除 ISO，再从磁盘启动。
6. 首次启动创建普通用户及独立的 root 密码，随后进入桌面。后续启动直接登录。

这是整盘安装，不保留所选盘上的旧文件或分区。安装器排除自己的启动介质、只读盘、
逻辑分区，以及内核尚无相应驱动的控制器。它不会声称某块盘“能被固件看到”就能被内核使用。
FAT32 只用于 64 MiB UEFI 启动分区；文件、账号和设置保存在剩余空间的 MNFS 分区。
首次账号流程目前继承 1–31 字符用户名、12–128 字符密码和字符类型规则；尚未完成
此前要求的无自定义长度限制，界面与认证层需要一起改动。

## 桌面操作

| 操作 | 方式 |
| --- | --- |
| 搜索应用 | 左上角 **Applications**，输入名称，点选或按 Enter |
| 切换窗口 | 顶栏任务按钮；拖动标题栏，`_` 最小化，`x` 关闭 |
| 文件浏览 | **Files**；Home、Up、New file、New folder、Delete |
| 编辑文件 | 点击文本文件；支持插入、删除、换行、方向键移动；Ctrl-S 保存 |
| 新建文本 | Files 的 New file；编辑器 Ctrl-N |
| 显示设置 | **Settings** 或 F7；R 切换受支持分辨率，W 壁纸，S 持久保存 |
| 终端 | **Terminal** 或 F5；普通操作按当前账号权限执行 |
| 锁定／电源 | Settings 中的 Lock session / Power |
| 帮助 | `help` 为英文，`help -cn` 为中文；也支持 `kernel help -cn` |

F1–F7 分别打开 Welcome、Files、Text editor、System monitor、Terminal、Devices、Settings。
Esc 显示桌面并最小化窗口，再点顶栏可恢复。编辑器未保存时关闭会要求确认。
文件管理器和编辑器使用同一套会话和路径权限，普通账号只能修改自己的目录。
编辑器目前为 16 KiB 文本文档编辑器；文件系统本身仍支持 64 位稀疏文件。
当前目录缓存最多 64 项，尚无完整的大目录分页功能。

Preview 2 使用比例字体的真实字宽排版正文，保留 1/64 像素精度；终端采用真正的等宽字体。
编辑器支持点击定位光标、按可见行上下移动、Tab 制表位，以及 UTF-8 文本的整字符移动和退格。
键盘输入目前仍为 ASCII，中文文本显示不代表已实现中文输入法。七款应用各有独立图标，
使用针对目标尺寸生成的抗锯齿图像；原生渲染器不使用 Python、浏览器或外部桌面框架。

字库覆盖 49,776 个 Unicode 码位，包含可打印 ASCII、中日韩文字及多种字母和符号。
`kernel fonts` 显示实际覆盖数，`kernel unicode` 渲染多语言样例；尚无复杂文字塑形、
双向排版或输入法。`help` 默认英文，`help -cn all` 展开 42 项中文命令说明，
`help -cn kernel display` 查看单项参数、权限与示例。Tab 补全命令，上下键翻阅输出。

## 已验证配置

| 架构 | 固件／机器 | 存储 | 显示和输入 |
| --- | --- | --- | --- |
| AMD64 | QEMU PC、OVMF UEFI、2 vCPU、1 GiB | 512 字节扇区 IDE | 标准 VGA/GOP，PS/2 键鼠 |
| ARM64 | QEMU virt、GICv3、UEFI/FDT、2 vCPU、1 GiB | VirtIO MMIO block | ramfb/GOP，VirtIO MMIO 键鼠 |

自动测试在新建空盘上执行：取消安装并逐字节检查磁盘未变、确认安装、回读验证、
移除 ISO、磁盘启动、建立账号、通过实际键盘设备打开文件和保存、通过鼠标打开应用菜单、
原生 ELF 运行、普通用户越权写入被拒绝、再次重启登录并读回文件和设置。
`desktop-validation.tar.gz` 包含日志及 QEMU 实际屏幕截图。

UTM 应使用 **QEMU 后端**并匹配上表设备；Apple Virtualization.framework 尚未验证。
ARM64 当前原生驱动支持 VirtIO **MMIO**，不是 VirtIO PCI。实体机器也必须有已实现的
启动、存储、输入和显示设备，不能从 QEMU 通过推断任意实体电脑均可运行。
Secure Boot 签名和原生 AHCI/NVMe、USB、网络、Wi-Fi、音频及加速 GPU 驱动尚未完成。

## 实现边界

这是可安装的基础原生 GUI 系统，仍是开发预览版。桌面和内置应用当前运行在内核内，
尚非隔离的用户态桌面服务；独立原生 ELF 程序具有自己的地址空间和权限检查。
尚不能运行 Firefox、VS Code、Linux CTF 工具或一般 Linux 应用。APFS 读写未实现。
`v2.0-installer-preview.3` 将 Debian/Linux/Xfce 工具环境和原生引导入口放进一份安装包。
浏览器和驱动运行在 Linux 环境，其兼容性不计入原生内核。本页描述的原生专用 ISO
仅安装 MNFS 与原生桌面；它不包含 Linux 应用。

原生源代码采用 GPL-3.0-or-later。Noto 和 DejaVu 字体是保留许可证的外部字体；编译器、QEMU、
镜像打包工具仅用于构建／测试，不是运行内核。新桌面安装路径使用本项目的 UEFI 加载器，
无需 GRUB；旧 BIOS 诊断介质仍使用 GRUB。

## 从源码复现

```sh
docker build -t mnos-native-builder -f native/Builder.Dockerfile .
docker run --rm --user "$(id -u):$(id -g)" --network none -v "$PWD/native:/native" -w /native \
  mnos-native-builder bash tests/desktop_ci.sh
```

输出在 `native/build/desktop/`。验证需要 QEMU、OVMF 和 ARM64 UEFI；没有执行 Python。
