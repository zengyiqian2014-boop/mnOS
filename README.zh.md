# mnOS

**双启动开发版：**同一张 ISO 内提供[自研 mnOS 内核](native/README.md)与
Debian 13 / Linux LTS 工具环境，分别构建 QEMU 和实体硬件版本。
自研内核用 C 和汇编编写，已有图形界面、用户态隔离与调度，仍处于早期阶段。
Firefox、CTF 工具、完整系统管理和常见硬件驱动在 Linux 入口使用。
参见[构建与使用说明](live/README.md)、[安全与性能设计](live/DESIGN.md)。
原有的最小 BusyBox 版本仍保留，下面的内容介绍原版构建方式。

[English](README.md) | [中文](README.zh.md)

一个从零构建的最小 Linux 发行版：**Linux 6.13.3 + busybox + GRUB**，完整的 FHS 目录树，打包为可直接启动的 ISO（BIOS + UEFI 双引导），开机会显示一张彩色的系统信息卡片。

![mnOS 启动画面](assets/screenshot.png)

## 特性

- 完整 FHS 目录结构（`/bin /etc /usr /var /proc /sys ...`）
- busybox 用户空间，400+ applet（sh、vi、top、grep、awk...）
- 开机自动进入 shell（无登录），启动日志结束后**清屏并显示系统信息卡片**
- 彩色提示符 `[\W]#`，以及快捷命令 `off`（关机）`rb`（重启）`ll`（列文件）
- GRUB 菜单与内核 framebuffer 同为 **1280×720（16:9）**
- 串口 / VGA 双控制台输出，`ls`、`mneofetch` 等命令两边都能正常显示
- 静态根文件系统打包，启动快（约 3 秒到 shell）
- 本地 **`ai`** 对话机器人：纯 Python 实现的 MiniMind2（llama）CPU 推理，输入 `ai` 即可聊天

## 运行

构建后可以直接启动，脚本默认读取仓库上一级目录中的 ISO：

```bash
./run.sh                          # QEMU 显示窗口，1024 MiB 内存
./run.sh --serial                  # 终端模式；先按 Ctrl-A，再按 X 退出 QEMU
./run.sh --memory 2048 /path/to/mnOS.iso
```

能访问 `/dev/kvm` 时使用 KVM，否则使用 QEMU 软件模拟。
可以用 `QEMU=/path/to/qemu-system-x86_64` 指定 QEMU 程序。

也可以直接调用 QEMU：

```bash
# 图形模式（GRUB 菜单 + VGA 控制台）
qemu-system-x86_64 -enable-kvm -m 512 -cdrom mnOSv1.7.iso

# 串口模式（终端里直接交互）
qemu-system-x86_64 -enable-kvm -m 512 -cdrom mnOSv1.7.iso -nographic
```

> 内置的 `ai` 模型需要更多内存 —— 运行 `ai` 前请用 `-m 1024` 启动 QEMU。

### 可选桌面

![QEMU 中实际运行的 mnOS 桌面](assets/desktop.png)

```bash
WITH_DESKTOP=1 DEFAULT_DESKTOP=1 ./build.sh
./run.sh
```

桌面直接使用 Linux framebuffer，提供终端入口、实时内存与运行时间、
快捷键帮助和关机／重启确认。按 **T** 打开终端，输入 `exit` 返回桌面；
Tab／方向键与 Enter 选择卡片，**S** 查看系统状态，Esc 返回或退出到控制台。
鼠标需要内核提供 `/dev/input/mice`。原内核可用键盘操作，下面的可选 QEMU
内核也启用了鼠标支持。

需要 Linux 虚拟终端和至少 640×480 的真彩色 framebuffer。在已运行的 Shell
里输入 `start-desktop`，可在 tty2 启动桌面。串口终端和 GRUB 原控制台菜单保留；
不设置 `DEFAULT_DESKTOP=1` 就仍以控制台为默认入口。文件仍保存在内存里。

构建桌面需要 C 编译器及静态 libc 开发文件（Debian 可安装 `gcc libc6-dev`），
无需显示服务器或额外 GUI 运行库。字体使用随源码附带、保留署名的公有领域位图字体。

### 可选 QEMU 内核

默认仍使用仓库中的 `boot/bzImage`。从 kernel.org 下载并解压 **Linux 6.13.3**
后，可以另行编译用于 QEMU 的内核：

```bash
JOBS=4 ./tools/build-qemu-kernel.sh /path/to/linux-6.13.3 /path/to/kernel-build
KERNEL_IMAGE=/path/to/kernel-build/arch/x86/boot/bzImage \
  WITH_DESKTOP=1 DEFAULT_DESKTOP=1 ./build.sh
```

该配置以 `x86_64_defconfig` 为基础，支持最多 8 个虚拟 CPU，启用体积优化，
去掉音频、无线、媒体和实体显卡驱动，保留 Bochs／virtio 显示与输入驱动，
补上鼠标设备，并通过 `/proc/config.gz` 导出配置。它面向 QEMU，其他硬件请使用
默认内核。最终配置也保存在输出目录的 `.config` 中。编译依赖
`make gcc bc flex bison libelf-dev libssl-dev` 及 libc 开发文件。

开发检查：

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
GEN_INIT_CPIO=/path/to/gen_init_cpio ./tests/test-build-mountpoints.sh
cc -fsanitize=address,undefined -g tests/test_desktop.c -o /tmp/test-desktop
/tmp/test-desktop
```

## 构建

### 一键构建

```bash
./build.sh          # 重新打包 initramfs 并生成 ../mnOSv1.7.iso
```

依赖：`grub-mkrescue`、`cpio`、`gzip`，以及内核源码里的 `gen_init_cpio`
（可用 `GEN_INIT_CPIO=/path/to/gen_init_cpio` 覆盖，`OUT=...` 指定输出路径，
`KERNEL_SRC=...` 指定内核源码目录）。

### 前置组件（内核 / busybox）

#### 1. 内核

```bash
git clone --depth=1 -b v6.13.3 https://github.com/torvalds/linux.git
cd linux
make olddefconfig          # 关键项: BLK_DEV_INITRD, DEVTMPFS, VT, DRM_BOCHS,
                            # FRAMEBUFFER_CONSOLE, SERIAL_8250_CONSOLE, ISO9660_FS
make -j$(nproc)
cp arch/x86/boot/bzImage ../
```

#### 2. busybox

```bash
git clone https://git.busybox.net/busybox
cd busybox
make defconfig             # 动态链接（CONFIG_STATIC is not set）
make -j$(nproc)
make CONFIG_PREFIX=../mnOS install
```

根文件系统需要带上 busybox 依赖的动态库（glibc）：

```bash
mkdir -p mnOS/lib/x86_64-linux-gnu
cp -a /lib/x86_64-linux-gnu/{libc,libm,libresolv,libnss_files,libnss_dns}-2.31.so* \
      /lib/x86_64-linux-gnu/ld-2.31.so mnOS/lib/x86_64-linux-gnu/
ln -sf /lib/x86_64-linux-gnu/ld-2.31.so mnOS/lib64/ld-linux-x86-64.so.2
printf '/lib/x86_64-linux-gnu\n/usr/lib/x86_64-linux-gnu\n' > mnOS/etc/ld.so.conf
ldconfig -r mnOS
```

#### 3. initramfs（无需 root）

设备节点不用 `mknod`，用内核自带的 `gen_init_cpio` 写进 cpio 头即可：

```bash
cat > devspec <<'EOF'
dir /proc 0755 0 0
dir /sys 0755 0 0
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/tty      0666 0 0 c 5 0
nod /dev/tty0     0600 0 0 c 4 0
nod /dev/tty1     0600 0 0 c 4 1
nod /dev/ttyS0    0666 0 0 c 4 64
nod /dev/null     0666 0 0 c 1 3
nod /dev/zero     0666 0 0 c 1 5
nod /dev/random   0666 0 0 c 1 8
nod /dev/urandom  0666 0 0 c 1 9
nod /dev/ptmx     0666 0 0 c 5 2
EOF
linux/usr/gen_init_cpio devspec > dev.cpio

# 两个归档拼接：设备节点在前，文件系统在后（内核支持拼接 cpio）
cd mnOS
find . -path ./boot -prune -o -path ./.git -prune -o -print | cpio --owner 0:0 -H newc -o > ../tree.cpio
cd ..
cat dev.cpio tree.cpio | gzip -9 > mnOS/boot/initramfs.cpio.gz
```

#### 4. 打包 ISO

```bash
mkdir -p staging/boot/grub
cp mnOS/boot/bzImage        staging/boot/
cp mnOS/boot/initramfs.cpio.gz staging/boot/

cat > staging/boot/grub/grub.cfg <<'EOF'
insmod all_video
if loadfont unicode; then
    set gfxmode=1280x720
    set gfxpayload=1280x720
    terminal_output gfxterm
fi
set timeout=5
set default=0
menuentry "mnOS" {
    linux /boot/bzImage video=Virtual-1:1280x720 console=tty0 console=ttyS0,115200
    initrd /boot/initramfs.cpio.gz
}
EOF

grub-mkrescue -o mnOSv1.7.iso staging/
```

## 目录结构

```text
mnOS/
├── init            # PID 1：挂载 proc/sys/devtmpfs/pts 后 exec /sbin/init
├── bin/            # busybox 及 applet 符号链接（含 mneofetch 信息卡片）
├── sbin/           # init、getty 等系统命令
├── etc/
│   ├── inittab     # sysinit + 双控制台 shell
│   ├── init.d/rcS  # 挂载文件系统 → 等待日志静默 → 清屏 → 输出信息卡片
│   ├── profile     # 彩色 PS1、快捷命令 alias
│   └── ...
├── boot/           # bzImage + initramfs.cpio.gz
├── opt/ai/         # MiniMind2 推理引擎 + 模型 + numpy/regex
├── assets/         # README 截图
├── build.sh        # 一键构建脚本
├── lib/, lib64/    # glibc 运行库与动态链接器
└── proc/ sys/ dev/ ...  # 标准 FHS 空目录（运行时挂载）
```

## 技术要点

| 问题 | 解决方式 |
|------|----------|
| 非 root 无法 `mknod` | 用内核的 `gen_init_cpio` 生成含设备节点的 cpio |
| 提供 `/init` 后内核不再自动挂 devtmpfs | 在 `/init` 里手动挂载 proc/sysfs/devtmpfs |
| `/dev/console` 指向哪个控制台 | 取决于 `console=` 参数顺序，`ttyS0` 放最后 |
| DRM 忽略 GRUB 的 `gfxpayload` 分辨率 | 用 `video=Virtual-1:1280x720` 强制指定 |
| GRUB 主题整段失效 | 剔除 `terminal-*` 字段（会导致主题解析失败） |
| 拼接 cpio 被当作普通文件打进归档 | 分别生成归档后 `cat` 拼接，不要混入 `find` 管道 |

## 许可证

mnOS 自有的代码（`init` 进程、`build.sh`、`etc/` 下的启动脚本以及
`bin/mneofetch`）采用 **GPL-3.0-or-later** —— 见 [LICENSE](LICENSE)。

mnOS 镜像以**聚合**形式分发：其中捆绑了多个第三方软件，各自适用其原有
许可，主要包括 Linux 与 BusyBox（GPL-2.0-only）、glibc（LGPL-2.1-or-later）、
GRUB、nano 与 GNU make（GPL-3.0-or-later）、Vim（Vim License）、
Python（PSF License）、tcc（LGPL-2.1）、Git（GPL-2.0）、NumPy（BSD-3-Clause）、
regex 与 MiniMind2 模型（Apache-2.0）。完整清单、上游
源码及"对应源码"获取方式见 [NOTICE](NOTICE)。整个镜像不作为一个整体重新授权。
