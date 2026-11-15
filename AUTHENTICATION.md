# 登录、验证器与人脸验证 / Authentication

这是源码中的新认证功能，**旧发布页上的 ISO 不会因此自动更新**。
密码和验证器功能已有自动化验证。Linux Full 的普通摄像头验证已接入 PAM，
作为密码及已开启 TOTP 之后的附加验证；它不具备可信 3D 深度或经认证的防伪能力。

## 安装与设置

| 环境 | 当前行为 |
| --- | --- |
| Linux 安装器 | 创建密码账号，可勾选开启验证器；向目标硬盘写入账号后，在安装窗口扫码或手动填写密钥，输入正确验证码后才启用。取消或失败会明确保留密码登录。 |
| Native 安装后的首次账号设置 | 创建普通用户和独立 root 密码后，选择 Y 绑定验证器，或 N 跳过。 |
| Linux 系统设置 | 打开“登录安全”或 `mnos-security`，开启、关闭验证器。关闭需要当前密码和第二步验证。 |
| Native 系统设置 | F7 → Enable/Disable 2FA，或 `2fa enable` / `2fa disable` / `2fa status`。`help 2fa` 为英文，`help -cn 2fa` 为中文。 |
| 精简版 Lite | 初始镜像仅密码验证，不安装 Linux Login Process、PAM 2FA 或摄像头组件。安装完整版功能包后可在设置中启用。 |
| 完整版 Full 安装环境 | 安装时绑定的验证器属于目标系统；不把账号秘密写回 ISO。 |

二维码和手动密钥是同一个本机生成的 TOTP 秘密。二维码不会上传到第三方网站。
手动添加时选择 **基于时间、SHA-1、6 位、30 秒**。密钥只在绑定页面显示。
绑定成功后显示八个一次性恢复码；它们代替验证码，**仍然需要账号密码**。
请保存在另一处；程序不提供再次导出密钥或恢复码的接口。

先验证密码，再验证验证码或恢复码。错误密码不会进入第二步。
成功使用的时间步或恢复码必须先可靠落盘，才会放行；磁盘保存失败时拒绝认证。
有时钟故障时可使用未用过的恢复码；绑定新验证器需要正确 UTC 时间。

Native 更改密码时会重新加密已绑定的密钥。
已有 Full 账号若启用了验证器，不能通过改用 Native Lite 绕过第二步，必须启动 Full。
Linux 目前会通过 PAM 阻止在绑定期间修改密码，避免密码与加密密钥不同步：
**先在“登录安全”关闭验证器和人脸验证，再更换密码，最后重新绑定**。这是当前实现限制。
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

## 普通摄像头登录的实际范围

Linux Full 的“登录安全”可录入、测试或关闭人脸验证。安装器中的选项会在
目标系统首次登录后邀请录入，取消不会启用验证。Lite 不安装这些组件；
安装完整版功能包后可开启。旧预览的用户目录模板不会自动升级为登录凭据，
需要重新录入并确认保存恢复码。

命令为 `mnos-face-preview --enroll|--test|--delete`，保留原命令名以兼容菜单。
它是普通用户运行的 C++ 设置界面。`face-helper` 是小型 C++ 权限代理，
不链接 GTK/OpenCV；它验证真实调用者、密码和已绑定的 TOTP，锁定账号记录，
启动专用 `login-camera` 无登录账号运行的摄像头进程。摄像头进程无 root 权限、
无新特权，不接收密码或 TOTP 秘密，仅在私有 IPC 中接收待匹配特征。
PAM 只信任代理启动的进程结果，不接受设置界面提供的“识别成功”标记。

识别使用本地 V4L2 摄像头及 OpenCV YuNet/SFace，检查人脸相似度、随机动作
顺序、眨眼、双向转头和圆环变色后的脸部颜色响应。没有 Python/Java 实现。
模型来源、校验值和许可证见 `live/face/`。不保存或上传照片、录像。
加密模板由 Login Process 账号保管，路径为
`/var/lib/mnos-login-process/faces/UID.face`，目录 0700、文件 0600。
使用独立随机盐、密码派生密钥及 XChaCha20-Poly1305，绑定 UID 和版本。

录入完成后显示八个独立的人脸恢复码。确认保存后才启用；每个恢复码只替代
一次人脸检查，仍需密码以及已开启的 TOTP。相机未转发、没有图形显示或检测
失败时可使用恢复码。TOTP 恢复码和人脸恢复码是两组不同的码。改变密码前必须
关闭两种验证。关闭人脸验证需要密码、已开启的 TOTP，以及人脸或人脸恢复码。

2.2.0~preview6 的相机进程枚举 `/dev/videoN`，查询设备自身的 V4L2 能力，
排除元数据和输出节点，尝试能够提供彩色画面的采集设备。第一台打开失败时
继续尝试下一台，不再固定使用 `/dev/video0`。窗口显示实际选中的设备名与路径。
缺少模型、权限不足、设备占用、没有可用画面会显示相应原因，不通过认证。
这些改动不提供 Native 摄像头驱动，也不把自动化设备选择测试当作实体相机测试。

启用后，使用 common-auth 的密码登录、锁屏与提权会额外验证人脸。
PAM 相机页面只连接本地 X11 显示；当前集成针对 LightDM/Xfce，读取固定的
LightDM 授权文件或账号 .Xauthority，并将只读副本传给低权限进程。
Wayland、无显示的 SSH/TTY 会话需要人脸恢复码。SSH 公钥等绕过 common-auth
的认证流程不在此保证范围内。其他用户无法查询、改写或删除此账号的凭据。

**普通 RGB 摄像头不是硬件 3D 人脸传感器。** 动作和颜色检查不能保证抵御
精心制作的视频或深伪。自动化测试覆盖真正的代理、PAM、模板加密与恢复流程，
成功相机结果用明确标记的测试进程模拟；模型推理、空画面和缺失相机另行执行。
尚无实体摄像头在不同光线、肤色及攻击样本下的测量结果，不宣称防伪认证达标。
Native 新增[原生 xHCI/UVC 预览驱动](native/USB.md)，可以在自研 GUI 内采集
YUY2 画面；尚无人脸检测、身份比对及活体验证，因此仍不提供原生人脸登录。
Native TOTP 使用其自身内核服务。虚拟摄像头传输测试不代表实体相机或人脸认证通过。

## 复现测试

Native 主机测试含 RFC 标准向量、独立 OpenSSL 验证、密文篡改、UID 绑定、
重放拒绝、恢复码单次使用、密码重包、取消、超时和保存失败。
真实 QEMU 测试从 ISO 安装后移除介质，读取屏幕二维码，通过独立验证器计算
验证码，重启确认持久化、密码单独不足以登录、错误验证码拒绝、正常登录与关闭。

```sh
docker build -t mnos-native-builder -f native/Builder.Dockerfile .
docker run --rm --init --network none -v "$PWD/native:/native" -w /native \
  mnos-native-builder bash tests/desktop_ci.sh

docker build -t mnos-auth-test -f live/tests/Authentication.Dockerfile .
docker run --rm --init --network none -e MNOS_AUTH_TEST_CONTAINER=1 \
  -v "$PWD:/source" -w /source/live mnos-auth-test bash tests/authentication.sh
```

Linux 测试在临时容器中创建测试账号，实际运行服务、broker 和 PAM，验证权限、
密码优先、TOTP、恢复码、断开服务、保存失败和取消安装绑定。
GTK 测试用 ZBar 识别真实窗口里的二维码，并核对手动密钥一致性。
测试脚本会修改容器用户和 PAM 配置，**只应在上述一次性容器中运行**。
它们不等于整张 Linux ISO 的启动测试；发布新 ISO 仍需执行完整安装/拔盘启动检查。

## English summary

Native and Linux Full now support optional password-first RFC 6238 TOTP enrollment,
local QR/manual pairing, encrypted secrets, one-use recovery codes and durable
replay rejection. Lite provides password-only setup; the Linux full feature
package restores the optional authentication components without reinstalling.
Linux changes require password reauthentication through a small privileged C
broker; the dedicated unprivileged Login Process owns the encrypted records.
Native provides the corresponding original C kernel service and GUI commands.

Linux Full adds password-first face verification through PAM, in addition to
any enabled TOTP. A dedicated unprivileged camera worker, protected encrypted
records and separate one-use face recovery codes are used. Ordinary RGB cameras
do not provide trusted depth or certified spoof resistance; physical-camera
validation remains outstanding. Native now has original xHCI/UVC preview support,
but no native face detection, matching or liveness authentication. Software-only
isolation does not protect live secrets against a malicious root/kernel.
Source changes do not automatically update previously published ISOs.
