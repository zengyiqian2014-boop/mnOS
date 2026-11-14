mnOS 的签名 APT 更新资源。此页面发布组件包和完整版功能包，不冒充新的安装 ISO。

[统一下载目录：完整版 / 精简版 / 补丁，按架构分类](https://github.com/zengyiqian2014-boop/mnOS/tree/resources)。

已配置此软件源的新系统：

```sh
sudo mnos full                 # 精简版补装完整版功能
sudo mnos full --without code  # 补装时排除列出的软件包
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
此认证版本仍属预览：人脸功能是录入和测试，尚未用于系统解锁。
自研版当前的 RV64 客体尚无 APT/虚拟网卡；不能把这些 Linux 软件包装进自研内核。

公钥指纹：`117234CA3D793137BE651EC6D2F2944324FA4E65`。
软件源：`https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/`。
