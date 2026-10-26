mnOS Linux Edition Preview：独立的 Linux 版离线安装包，提供 AMD64 与 ARM64。每个架构一份完整 ISO，面向文档列明的虚拟机与实体硬件。

- 自研内核版与 Linux 版分别交付。本页镜像启动 Debian Linux/Xfce，不再把自研内核混在 Linux 安装菜单中。自研版中的 Linux 客体是另一套真实的软件模拟运行环境，范围见仓库 EDITIONS.md。
- 新增 C 实现的 `mnos-admin`：系统概况、进程、存储、网络、账户、服务、日志、启动、安全和工具入口。状态查询遵守普通账号权限；服务启停等修改明确要求 sudo，不自动提权。
- “mnOS 系统管理中心”集中提供服务、账户、磁盘、网络、日志、更新、内核设置、命令和工具图形入口。底层系统管理器仍来自各自上游项目，不冒称自研内核功能。
- 保留 Firefox ESR、官方 VS Code、开发环境、GNU/Linux 命令库及完整 CTF 工作台。命令目录读取本机实际安装程序；工具名、分类、离线帮助可搜索。没有固定的“Linux 所有命令”全集。
- `help` 默认英文，`help -cn` 中文；新增 `help editions`、`help management`、`mnos-admin help -cn`。软件运行层与权限在系统内手册写明。
- mnOS 新代码使用 C/C++、汇编和 shell 构建脚本；Python 仅作为必要的第三方工具依赖保留。

这是开发预览版。发布需要两架构完整构建、图形安装、移除 ISO 后磁盘启动、账号认证、VirtIO/NVMe、AMD64 BIOS、Firefox 离线渲染、VS Code 实际编辑保存和 CTF 图形工具检查通过。具体源码、镜像校验和及测试运行记录随发布提供。

安装会清空用户明确选择并再次确认的目标盘。ARM64 针对 QEMU virt 和受支持 UEFI 平台；UTM 使用 QEMU 后端。未验证 Apple Virtualization.framework、任意 ARM 开发板或全部真实硬件。暂无安全认证、全硬件兼容或性能优于 Linux/RHEL 的结论。
