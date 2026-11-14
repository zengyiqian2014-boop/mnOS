# mnOS 统一资源入口

完整版、精简版、补丁都在这里，按类别、系统和 CPU 架构列出；安装 ISO 的具体测试范围见对应发布说明。
Linux Edition 使用 Linux 内核；Native Edition 使用自研内核，当前 Linux 客体尚无 APT / 网络 / GUI 应用兼容。

```sh
mnos list                  # 所有已发布资源
mnos list full             # 完整版安装镜像
mnos list lite             # 精简版安装镜像
mnos list patch            # 功能包与组件补丁
mnos packages              # 本机软件包及版本
mnos packages full         # Full 组件选择清单
sudo mnos update           # 先校验 Raw 版本索引，再更新相应组件
sudo mnos full --without code
help -cn updates
```

[签名 Raw 索引](https://raw.githubusercontent.com/zengyiqian2014-boop/mnOS/resources/catalog.json.asc) · [JSON 目录](catalog.json)

`--without` 后的包本次不安装，依赖冲突会停止；已安装的包不会被卸载。
更新与回退只管理 mnOS 组件，不会用另一架构或另一系统的 ISO 覆盖当前安装。
旧版本保留以便明确选择回退；预览版不等于经过所有硬件验证的稳定版。

## 完整版 / Full

| 系统 | 架构 | 版本 | 下载 |
| --- | --- | --- | --- |
| linux | amd64 | v2.1-linux-preview.1 | [mnOS-2.1-linux-preview.1-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v2.1-linux-preview.1/mnOS-2.1-linux-preview.1-amd64.iso) |
| linux | arm64 | v2.1-linux-preview.1 | [mnOS-2.1-linux-preview.1-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v2.1-linux-preview.1/mnOS-2.1-linux-preview.1-arm64.iso) |
| native | amd64 | v0.2-native-preview.1 | [mnOS-native-x86_64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.2-native-preview.1/mnOS-native-x86_64.iso) |
| native | amd64 | v0.3-native-preview.1 | [mnOS-native-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.3-native-preview.1/mnOS-native-amd64.iso) |
| native | amd64 | v0.3-native-preview.2 | [mnOS-native-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.3-native-preview.2/mnOS-native-amd64.iso) |
| native | amd64 | v0.5-native-preview.1 | [mnOS-native-0.5-preview.1-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.5-native-preview.1/mnOS-native-0.5-preview.1-amd64.iso) |
| native | amd64 | v0.5-native-preview.2 | [mnOS-native-0.5-preview.2-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.5-native-preview.2/mnOS-native-0.5-preview.2-amd64.iso) |
| native | amd64 | v0.6-native-preview.1 | [mnOS-native-0.6-preview.1-amd64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.6-native-preview.1/mnOS-native-0.6-preview.1-amd64.iso) |
| native | arm64 | v0.2-native-preview.1 | [mnOS-native-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.2-native-preview.1/mnOS-native-arm64.iso) |
| native | arm64 | v0.3-native-preview.1 | [mnOS-native-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.3-native-preview.1/mnOS-native-arm64.iso) |
| native | arm64 | v0.3-native-preview.2 | [mnOS-native-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.3-native-preview.2/mnOS-native-arm64.iso) |
| native | arm64 | v0.5-native-preview.1 | [mnOS-native-0.5-preview.1-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.5-native-preview.1/mnOS-native-0.5-preview.1-arm64.iso) |
| native | arm64 | v0.5-native-preview.2 | [mnOS-native-0.5-preview.2-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.5-native-preview.2/mnOS-native-0.5-preview.2-arm64.iso) |
| native | arm64 | v0.6-native-preview.1 | [mnOS-native-0.6-preview.1-arm64.iso](https://github.com/zengyiqian2014-boop/mnOS/releases/download/v0.6-native-preview.1/mnOS-native-0.6-preview.1-arm64.iso) |

## 精简版 / Lite

| 系统 | 架构 | 版本 | 下载 |
| --- | --- | --- | --- |

## 补丁与功能包 / Packages

| 系统 | 架构 | 版本 | 下载 |
| --- | --- | --- | --- |
| linux | amd64 | 2.2.0~preview1 | [mnos-auth_2.2.0.preview1_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview1_amd64.deb) |
| linux | amd64 | 2.2.0~preview1 | [mnos-full_2.2.0.preview1_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview1_amd64.deb) |
| linux | amd64 | 2.2.0~preview1 | [mnos-runtime_2.2.0.preview1_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview1_amd64.deb) |
| linux | amd64 | 2.2.0~preview2 | [mnos-auth_2.2.0.preview2_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview2_amd64.deb) |
| linux | amd64 | 2.2.0~preview2 | [mnos-full_2.2.0.preview2_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview2_amd64.deb) |
| linux | amd64 | 2.2.0~preview2 | [mnos-runtime_2.2.0.preview2_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview2_amd64.deb) |
| linux | amd64 | 2.2.0~preview4 | [mnos-auth_2.2.0.preview4_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview4_amd64.deb) |
| linux | amd64 | 2.2.0~preview4 | [mnos-full_2.2.0.preview4_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview4_amd64.deb) |
| linux | amd64 | 2.2.0~preview4 | [mnos-runtime_2.2.0.preview4_amd64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview4_amd64.deb) |
| linux | arm64 | 2.2.0~preview1 | [mnos-auth_2.2.0.preview1_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview1_arm64.deb) |
| linux | arm64 | 2.2.0~preview1 | [mnos-full_2.2.0.preview1_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview1_arm64.deb) |
| linux | arm64 | 2.2.0~preview1 | [mnos-runtime_2.2.0.preview1_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview1_arm64.deb) |
| linux | arm64 | 2.2.0~preview2 | [mnos-auth_2.2.0.preview2_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview2_arm64.deb) |
| linux | arm64 | 2.2.0~preview2 | [mnos-full_2.2.0.preview2_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview2_arm64.deb) |
| linux | arm64 | 2.2.0~preview2 | [mnos-runtime_2.2.0.preview2_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview2_arm64.deb) |
| linux | arm64 | 2.2.0~preview4 | [mnos-auth_2.2.0.preview4_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-auth_2.2.0.preview4_arm64.deb) |
| linux | arm64 | 2.2.0~preview4 | [mnos-full_2.2.0.preview4_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-full_2.2.0.preview4_arm64.deb) |
| linux | arm64 | 2.2.0~preview4 | [mnos-runtime_2.2.0.preview4_arm64.deb](https://github.com/zengyiqian2014-boop/mnOS/releases/download/updates-stable/mnos-runtime_2.2.0.preview4_arm64.deb) |
