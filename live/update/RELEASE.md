mnOS 的签名 APT 更新资源。此页面发布组件包和完整版功能包，不冒充新的安装 ISO。

[统一下载目录：完整版 / 精简版 / 补丁，按架构分类](https://github.com/zengyiqian2014-boop/mnOS/tree/resources)。

已配置此软件源的新系统：

```sh
sudo mnos full                 # 精简版补装完整版功能
sudo mnos full --without code  # 补装时排除列出的软件包
sudo mnos --uninstall 包名     # 卸载普通软件，保护 C/C++/ASM 工具链
sudo mnos update               # 更新已安装的 mnOS 组件
apt-cache policy mnos-runtime  # 查看可用版本
sudo mnos rollback 版本号       # 回退仍保留在源中的 mnOS 组件版本
help -cn apt
help -cn updates
```

`mnos list` 列出统一目录中的所有资源；`mnos list full`、`mnos list lite`、
`mnos list patch` 按类别筛选。`mnos packages` 列出本机包与版本，
`mnos packages full` 列出 Full 功能组件。
`mnos update` 先抓取并验证 [GitHub Raw 版本索引](https://raw.githubusercontent.com/zengyiqian2014-boop/mnOS/resources/catalog.json.asc)，
再通过签名 APT 软件源安装索引指定的 mnOS 组件版本。不会把 ISO 当成更新脚本执行。

`--without` 后面的所有名称都表示本次不安装的软件包，例如
`sudo mnos apt install nano nmap --without nmap` 只请求安装 nano。
排除也作用于依赖：如果其他选定包必须依赖被排除的包，会停止并说明冲突。
不会卸载已经存在的软件包；这是本次安装选择，不是永久禁止后续 APT 安装。
带排除项的 `mnos full` 按组件安装，不安装强制依赖所有组件的 `mnos-full` 元包。

每个版本包含 AMD64 / ARM64 软件包。APT 会核对 `InRelease` 签名和软件包摘要。
完整版功能包可能安装第三方 Python 工具及其依赖；精简 ISO 初始环境不含 Python。
首次加入更新管理的旧系统可安装相应架构的 `mnos-runtime_*.deb`，之后使用上述命令。

回滚覆盖 mnOS runtime/auth/full 软件包，不回滚 Debian 内核、其他软件包或个人文件。
2.2.0~preview6 修复摄像头固定为 `/dev/video0` 的兼容问题：筛选 V4L2 采集节点，
排除元数据节点，第一台不可用时尝试下一台；界面显示实际设备与具体错误原因。
升级已有 Full 系统后运行 `mnos-security`，在“登录安全”录入或测试；这些组件
不自动开启人脸认证，也不会改写已有 ISO。Native 仍不支持摄像头登录。
2.2.0~preview3 的 Linux Full 组件加入 PAM 附加人脸验证、受保护的加密模板、
一次性人脸恢复码和语言/地区设置。摄像头需要本地 X11 环境；普通 RGB 摄像头
没有可信深度，实体摄像头和防伪测量尚未完成。具体发布状态以签名目录为准。
自研版当前的 RV64 客体尚无 APT/虚拟网卡；不能把这些 Linux 软件包装进自研内核。

公钥指纹：`117234CA3D793137BE651EC6D2F2944324FA4E65`。
软件源：`https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/`。
