# 给 micronull.cn 管理员：完整官网部署

网站地址：**https://mnos.micronull.cn/**

使用随附的**完整网站部署包**。这是完整的静态官网，包含首页、Linux
独立系统页、下载选择器、系统支持、使用说明、图片、样式及 3D 动画资源。
无音乐组件，不需要安装 Node.js、Python、数据库或启动后端服务。
ISO 文件保留发布页下载地址，没有装进网站 ZIP。

另附 `mnOS-website-source.zip`，保留网页源码、构建脚本、测试和许可，供后续维护。
直接上传时选择上面的静态网站包。绑定完成后，地址栏保持 `mnos.micronull.cn`。

**已有网站更新：** 打开现有的 `mnos-website` Pages 项目，选择 **Create deployment**，
上传这次的完整网站 ZIP，发布到 **Production**，然后点击 **Save and Deploy**。
继续使用现有域名和 DNS；发布完成后刷新首页、Linux 页及系统支持页，检查两个反馈邮箱。

## 在 Cloudflare Pages 上传

1. 登录管理 `micronull.cn` 的 Cloudflare 账号。
2. 进入 **Workers & Pages → Create application → Pages / Get started**，选择
   **Drag and drop your files / Upload assets**（上传静态文件）。这是 Pages 项目。
3. 项目名称填 `mnos-website`，上传 ZIP；如果界面不接受 ZIP，先解压，上传解压后
   的文件夹。检查上传根目录能直接看到 `index.html`、`linux.html`、`assets/`、
   `vendor/`。然后点 **Deploy site**。
4. 先打开 Cloudflare 提供的 `*.pages.dev` 地址，确认首页、图片、动画正常。
5. 进入该 Pages 项目 **Custom domains → Set up a custom domain**，输入
   **mnos.micronull.cn**，按页面提示确认 DNS，等待域名及证书状态变为 **Active**。
6. 打开 **https://mnos.micronull.cn/**，按下方清单验收。

只绑定 `mnos` 子域名，保留根域名和其他业务的 DNS 记录。
如果这个子域名已经绑定之前的 Worker 转接方案，先记录旧配置，再移除该 Worker
的同名 Custom Domain，随后绑定到 Pages。不要同时让两者占用这个地址。
不要在 GitHub Pages 设置同名自定义域名；此次由 Cloudflare Pages 托管完整网站。

## 上线后检查

- 首页能显示图片和 3D 介绍；可以暂停、继续和跳过，手机页面没有横向溢出。
- `/linux.html`、`/support.html`、`/terms.html` 都能直接访问。
- 原生系统与 Linux 独立系统的下载页各选各的镜像；检查 AMD64 / ARM64 和
  在线 / 离线选项。尚未发布的组合应显示不可下载，不能跳到别的镜像。
- 下载前出现使用说明确认；未勾选同意不能下载。
- 官网反馈入口同时列出 `report.mnos@proton.me` 和 `wrmfwv@163.com`，点击能打开邮件应用。
- 页面没有音乐按钮、播放器或音频请求。

## 后续更新和回滚

在同一个 Pages 项目选择 **Create deployment**，上传新版完整网站 ZIP，并发布到
**Production**。域名不用重新配置。这个上传方式不会自动同步 GitHub 的页面更新。
需要回滚时，在 **Deployments** 中选择之前的成功生产部署，使用 **Rollback**。

官网的图片、脚本和动画资源均随包上传。实时资源目录优先尝试 GitHub Raw，失败时
继续使用包内目录；ISO 下载仍由 GitHub Releases 提供。请在实际使用的网络验收速度。

## 从源码重新打包（由维护者执行）

在 mnOS 仓库根目录，用 Node.js 22 或更新版本执行：

```sh
MNOS_SITE_PUBLIC_URL=https://mnos.micronull.cn/ node website/scripts/build.mjs
cd _site
zip -qrFS ../mnOS-website-mnos.micronull.cn.zip .
```

构建会清空并重新生成 `_site/`，写入正确的新域名分享链接。
把 `_site` 的内容上传即可。保留 `vendor/THREE-LICENSE.txt` 中的第三方许可。

Cloudflare 官方操作说明：
https://developers.cloudflare.com/pages/get-started/direct-upload/
