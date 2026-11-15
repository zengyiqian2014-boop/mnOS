# mnos.micronull.cn 官网转接

目标地址：**https://mnos.micronull.cn/**

这份 Worker 把现有 GitHub Pages 官网接到新域名，地址栏保留新域名。
原站是 https://zengyiqian2014-boop.github.io/mnOS/ 。原站继续更新后，新站通常
一分钟内取得新页面，静态资源缓存最长五分钟。没有重新复制一个网站仓库。

## 给 micronull.cn 管理员

在管理 `micronull.cn` 的 Cloudflare 账号里操作：

1. 打开 **Workers & Pages**，创建 Worker，名称可用 `mnos-website`。
2. 在代码编辑器里把默认代码替换为本目录的 **worker.mjs**，保存并部署。
3. 打开此 Worker 的 **Settings → Domains & Routes → Add → Custom Domain**。
4. 填入 **mnos.micronull.cn**，点 **Add Custom Domain**，等待证书生效。
5. 用下面的验收命令检查，再打开网站确认动画、下载页正常。

Cloudflare 会为这个 Custom Domain 创建 DNS 记录和 TLS 证书。不需要修改
`micronull.cn` 根域名，也不需要把 GitHub Token 给 Cloudflare。
如果已有同名 CNAME，先核对其用途，再处理记录冲突。
单独添加一条指向 GitHub 的 CNAME 不会完成这里的路径转接。

**保持原 GitHub Pages 地址可用，不要把 GitHub Pages 的 Custom domain 改为这个
新域名，也不要给网站添加 CNAME 文件**：否则原站会跳回新域名，造成代理循环。

也可以在本目录用 Node.js 22 或更高版本部署：

```sh
npx wrangler@4 login
npx wrangler@4 deploy --config wrangler.toml
```

登录的是域名管理员的 Cloudflare 账号；配置文件已经包含子域名绑定。
无需在代码中填写账号密码或 API Token。请勿把凭据提交到 Git。

## 验收

```sh
# 首页和资源应返回 200；证书验证不能用 -k 跳过。
curl -I https://mnos.micronull.cn/
curl -I https://mnos.micronull.cn/linux.html
curl -I https://mnos.micronull.cn/assets/native-desktop.png
curl -I https://mnos.micronull.cn/catalog.json
# 应返回 308，Location 为 https://mnos.micronull.cn/linux.html。
curl -I https://mnos.micronull.cn/mnOS/linux.html
```

打开首页和 Linux 页面各检查一次：介绍动画、图片和导航可用；原生系统与 Linux
的下载选项各自独立；未同意使用说明时不能开始下载。手机上再检查布局和播放操作。
代码会把 HTML 内的 canonical / Open Graph 原站地址替换为新域名。

**域名配置必须由域名管理员完成。本仓库提供配置和本地测试，不代表域名已经上线。**

## 范围与排错

- 只转接这个官网的页面、脚本、图片及媒体，不提供任意 URL 代理。访问者的 Cookie、
  Authorization 和自带的 IP 转发头不会传给原站；不转发原站 Cookie。
- ISO、签名、实时资源目录以及官方音乐播放器保留原外部地址。新域名本身不会让
  GitHub 大文件或第三方音乐变成国内镜像。附带的资源目录通过本站加载，实时目录
  不可达时网页使用附带目录。
- 没有实测中国各地网络速度；需在用户实际网络验收。Cloudflare 普通代理也不等于
  中国大陆 CDN。不要仅因挂上域名就承诺下载或音乐加速。
- 出现 `Unknown website host`：确认访问的是绑定后的 `mnos.micronull.cn`；为避免
  误用，Worker 不在其他域名提供服务（包括 workers.dev）。
- 出现 `Unexpected website origin redirect`：检查是否误改了 GitHub Pages 自定义
  域名。恢复原 Pages 地址后再测，不能通过允许任意重定向来修复。
- 出现 503：原站超时或不可用，先检查原 GitHub Pages 地址；错误响应不会被缓存。
- 回滚：在 Cloudflare Worker 的部署历史中恢复先前版本；要撤掉新入口，从此
  Worker 删除这个 Custom Domain。原 GitHub Pages 网站继续独立运行。

## 本地检查

```sh
# 仓库根目录
node --test deploy/cloudflare/worker.test.mjs
node website/scripts/build.mjs
node deploy/cloudflare/preview.mjs
# 打开 http://127.0.0.1:4174/
```

预览服务器仅绑定本机，并把构建后的 `_site` 作为原站替身。它测试同一份 Worker
的路径和页面改写，但不代表 Cloudflare DNS、TLS、缓存或中国网络已通过实测。

Cloudflare 官方说明：
https://developers.cloudflare.com/workers/configuration/routing/custom-domains/
