mnOS Desktop 0.4 Preview 1 把原生图形桌面和离线安装流程接通。AMD64、ARM64 各下载一份 ISO 即可；镜像安装后直接启动 mnOS 自研内核与原生桌面。

已集成：应用菜单与搜索、多窗口、文件浏览／创建／删除、文本编辑器、终端、系统监视、设备信息、壁纸与显示设置、普通用户／root 认证、持久化文件和个人设置。内核、桌面及安装器使用 C／汇编；镜像没有 Linux、Xfce 或 Python 运行时。

安装器进入图形界面，选择磁盘、确认清空整盘并输入 `ERASE`，安装完成后移除 ISO，从磁盘启动。账号在首次启动创建。**`help` 默认英文；`help -cn` 为中文。**

发布前在 AMD64 OVMF/IDE 和 ARM64 UEFI/FDT/VirtIO-MMIO QEMU 中实际完成空盘安装、移除 ISO 启动、键盘／鼠标操作、文件编辑保存、再次重启后账号／文件／设置恢复和权限拒绝检查。取消安装会检查磁盘未写入。发布附源码、校验、日志和实际桌面截图；ISO 是本次成功验证的原始产物。

这是基础原生 GUI 的开发预览版：桌面服务仍在内核内；Firefox、VS Code、Linux 工具、APFS、AHCI/NVMe、USB、网络／Wi-Fi、音频和 GPU 加速尚未完成，不能当作已全部兼容的日常系统。UTM 使用匹配设备的 QEMU 后端，Apple Virtualization.framework 未验证。完整配置、账号／编辑器现有限制和安装说明见仓库 `native/DESKTOP.md`。

旧 v2.0 Linux/Xfce 安装包继续独立归档，不是本版本的底层或默认桌面。
