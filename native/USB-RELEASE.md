本次增加自研内核的 xHCI USB 主机驱动、UVC 视频采集和原生 GUI 预览，提供 AMD64 与 ARM64 安装 ISO。驱动直接使用 mnOS 的内存、DMA 和设备接口；这两个镜像没有 Linux 内核、Linux 模拟环境、V4L2 或 Python。

摄像头在启动时默认关闭。登录后打开 Devices → Start camera，或运行 `camera start`。`camera stop` 停止并清空画面；锁屏会停止采集，拔出设备后预览会关闭。`kernel usb` 查看设备，`camera status` 查看状态和帧计数。`help` 默认英文，`help -cn` 中文。

当前支持直接连接到 xHCI 根端口的 USB 2 高速摄像头，格式为 YUY2，最大 640×480。摄像头需在启动前连接；集线器、热插入、SuperSpeed 视频、MJPEG 和 USB 键鼠驱动尚未实现。使用现有 PS/2 或 VirtIO 输入设备。

发布产物通过 AMD64、ARM64 的 QEMU 安装、移除 ISO 后启动、bulk/isochronous 视频采集测试。测试摄像头发送已知图案，程序实际检查桌面截图中的像素，并验证停止、重启采集、锁屏及拔出设备。还执行桌面文件保存／重启恢复测试，以及带 ASan/UBSan 的协议解析和现有原生功能测试。附源代码、测试日志、截图和 SHA256。

这是原生驱动开发预览，不是已验证全部硬件的稳定发行版。**尚未测试实体摄像头；原生人脸身份识别和活体检测仍未完成。** 此 ISO 不包含 Firefox、VS Code 或 Linux CTF 应用。Linux 发行版仍独立提供，不作为此原生驱动的实现。

安装会要求选择目标磁盘并输入 `ERASE` 确认清空。安装后移除 ISO，从磁盘启动并创建账号。源码目录中的 `native/USB.md` 记录驱动范围、测试方法和限制。
