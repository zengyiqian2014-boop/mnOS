mnOS 的签名 APT 更新资源。此页面发布组件包和完整版功能包，不冒充新的安装 ISO。

已配置此软件源的新系统：

```sh
sudo mnos full                 # 精简版补装完整版功能
sudo mnos update               # 更新已安装的 mnOS 组件
apt-cache policy mnos-runtime  # 查看可用版本
sudo mnos rollback 版本号       # 回退仍保留在源中的 mnOS 组件版本
help -cn apt
help -cn updates
```

每个版本包含 AMD64 / ARM64 软件包。APT 会核对 `InRelease` 签名和软件包摘要。
完整版功能包可能安装第三方 Python 工具及其依赖；精简 ISO 初始环境不含 Python。
首次加入更新管理的旧系统可安装相应架构的 `mnos-runtime_*.deb`，之后使用上述命令。

回滚覆盖 mnOS runtime/auth/full 软件包，不回滚 Debian 内核、其他软件包或个人文件。
此认证版本仍属预览：人脸功能是录入和测试，尚未用于系统解锁。
自研版当前的 RV64 客体尚无 APT/虚拟网卡；不能把这些 Linux 软件包装进自研内核。

公钥指纹：`117234CA3D793137BE651EC6D2F2944324FA4E65`。
软件源：`https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/`。
