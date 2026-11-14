# 登录与验证器 / Authentication preview

这是源码中的新认证功能，**旧发布页上的 ISO 不会因此自动更新**。
密码和验证器功能已有自动化验证；普通摄像头功能目前是录入与测试预览，
尚未接入系统登录，不能宣传为已完成的人脸解锁或可信 3D 深度识别。

## 安装与设置

| 环境 | 当前行为 |
| --- | --- |
| Linux 安装器 | 创建密码账号，可勾选开启验证器；向目标硬盘写入账号后，在安装窗口扫码或手动填写密钥，输入正确验证码后才启用。取消或失败会明确保留密码登录。 |
| Native 安装后的首次账号设置 | 创建普通用户和独立 root 密码后，选择 Y 绑定验证器，或 N 跳过。 |
| Linux 系统设置 | 打开“登录安全”或 `mnos-security`，开启、关闭验证器。关闭需要当前密码和第二步验证。 |
| Native 系统设置 | F7 → Enable/Disable 2FA，或 `2fa enable` / `2fa disable` / `2fa status`。`help 2fa` 为英文，`help -cn 2fa` 为中文。 |
| ISO 安装环境 | 不配置人脸或 TOTP 登录凭据。安装时绑定的验证器属于目标系统，ISO 会话不因此启用第二步验证。 |

二维码和手动密钥是同一个本机生成的 TOTP 秘密。二维码不会上传到第三方网站。
手动添加时选择 **基于时间、SHA-1、6 位、30 秒**。密钥只在绑定页面显示。
绑定成功后显示八个一次性恢复码；它们代替验证码，**仍然需要账号密码**。
请保存在另一处；程序不提供再次导出密钥或恢复码的接口。

先验证密码，再验证验证码或恢复码。错误密码不会进入第二步。
成功使用的时间步或恢复码必须先可靠落盘，才会放行；磁盘保存失败时拒绝认证。
有时钟故障时可使用未用过的恢复码；绑定新验证器需要正确 UTC 时间。

Native 更改密码时会重新加密已绑定的密钥。
Linux 目前会通过 PAM 阻止在绑定期间修改密码，避免密码与加密密钥不同步：
**先在“登录安全”关闭验证器，再更换密码，最后重新绑定**。这是当前实现限制。
本机 PAM 的密码登录、sudo/su 和使用同一 PAM 配置的锁屏会执行第二步；
SSH 公钥、其他绕过 common-auth 的服务或用户自行替换的认证栈不在此保证范围内。

## 保存方式与权限边界

- TOTP：RFC 4226 / RFC 6238，32 字节安全随机秘密，Base32 编码仅用于绑定。
- 密码派生：PBKDF2-HMAC-SHA256，600,000 次迭代，独立随机盐。
  存储的密码验证值不会被用作解密密钥。
- 密钥加密：Monocypher 4.0.2 的 XChaCha20-Poly1305；每次保存生成新 nonce。
  UID、版本标识、派生盐、已接受时间步和恢复码摘要参与认证。
- 恢复码：每个 128 位随机值，只保存带随机盐的 HMAC-SHA256 摘要。
  其随机强度不同于低熵密码，不需要额外慢哈希。
- Linux 系统账号 `login-process`，显示名 **Login Process**，无登录 shell。
  `/var/lib/mnos-login-process` 为 0700，记录为 0600。root 专用 Unix socket
  检查真实对端凭据；小型 setuid C broker 再验证调用者账号，GUI 本身不提权。
  systemd 服务限制写入范围、设备、地址族和能力；禁止 core dump。
- Native 认证仍属于自研内核内的服务。凭据路径通过文件 API 保护，
  **它还不是独立地址空间里的 Login Process 进程**。

这些措施保护离线磁盘中的明文秘密，以及普通账号之间的访问。
它们**不能保证恶意 root、被修改的内核或能替换启动链的攻击者看不到运行时秘密**。
要建立更强的边界还需要可信启动、硬件密钥封装和相应硬件验证。
二维码、恢复码和摄像头预览是用户主动要求显示的敏感界面；屏幕拍摄、
受控桌面中的键盘记录、已登录会话的恶意程序不由静态磁盘加密解决。

Linux 使用内核 `getrandom()`，Native 使用现有的安全随机设备/CPU 随机源。
没有合格随机源时不生成密钥。认证状态迁移支持原 Native v1 账号记录；
保存后升级为 v2，不要求用户重新创建已有账号。

## 普通摄像头预览的实际范围

Linux 安装器可选择首次登录时打开采集；之后也可在“登录安全”录入、测试或删除。
`mnos-face-preview --enroll|--test|--delete` 使用 C++、OpenCV YuNet/SFace 和
本地 V4L2 摄像头。没有 Python/Java 实现。模型是有明确来源与许可证的第三方组件。

预览包括随机动作顺序、缓慢眨眼、双向转头和缓慢变色的圆环，检查面部区域的
颜色响应。用单目关键点估计头部姿态，不声称测得真实深度。
不同人脸、超时、停帧、动作或颜色响应不足会结束本次测试。
只有完成采集并点击完成，才保存加密的人脸特征；不保存照片或录像。
模板以独立随机盐和密码派生密钥加密，位于用户私有目录
`~/.local/share/mnos/security/face-v1`。更改密码后需要重新录入模板。

**该预览不会解锁系统，也不会替代密码/TOTP。** 当前只有合成观测的状态机测试，
尚无实体摄像头、不同光照/肤色、照片、屏幕重放或深伪攻击的测量结果。
接入真正的人脸登录前仍需完成这些验证和可信摄像头到认证服务的数据通路。
Native 尚无 USB/UVC 摄像头驱动，因此不提供该摄像头页面。

## 复现测试

Native 主机测试含 RFC 标准向量、独立 OpenSSL 验证、密文篡改、UID 绑定、
重放拒绝、恢复码单次使用、密码重包、取消、超时和保存失败。
真实 QEMU 测试从 ISO 安装后移除介质，读取屏幕二维码，通过独立验证器计算
验证码，重启确认持久化、密码单独不足以登录、错误验证码拒绝、正常登录与关闭。

```sh
docker build -t mnos-native-builder -f native/Builder.Dockerfile .
docker run --rm --network none -v "$PWD/native:/native" -w /native \
  mnos-native-builder bash tests/desktop_ci.sh

docker build -t mnos-auth-test -f live/tests/Authentication.Dockerfile .
docker run --rm --network none -e MNOS_AUTH_TEST_CONTAINER=1 \
  -v "$PWD:/source" -w /source/live mnos-auth-test bash tests/authentication.sh
```

Linux 测试在临时容器中创建测试账号，实际运行服务、broker 和 PAM，验证权限、
密码优先、TOTP、恢复码、断开服务、保存失败和取消安装绑定。
GTK 测试用 ZBar 识别真实窗口里的二维码，并核对手动密钥一致性。
测试脚本会修改容器用户和 PAM 配置，**只应在上述一次性容器中运行**。
它们不等于整张 Linux ISO 的启动测试；发布新 ISO 仍需执行完整安装/拔盘启动检查。

## English summary

Native and Linux now support optional password-first RFC 6238 TOTP enrollment,
local QR/manual pairing, encrypted secrets, one-use recovery codes and durable
replay rejection. ISO sessions do not enroll their own additional login factors.
Linux changes require password reauthentication through a small privileged C
broker; the dedicated unprivileged Login Process owns the encrypted records.
Native provides the corresponding original C kernel service and GUI commands.

The ordinary-camera feature is explicitly an enrollment/testing preview, not a
system login factor. Software-only isolation does not protect runtime secrets
against a malicious root or kernel. This source change does not update existing
release ISOs. See the sections above for tested scope and remaining work.
