#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run only from the authenticated publishing workflow. No rebuilding or overlays.
set -euo pipefail
case "${FLAVOR:?}" in
  full) workflow=installer.yml; job_prefix='build ('; artifact_prefix=mnOS-installer; suffix='' ;;
  lite) workflow=lite.yml; job_prefix='linux ('; artifact_prefix=linux-lite; suffix=-lite ;;
  *) exit 2 ;;
esac
[[ "${BUILD_RUN:?}" =~ ^[0-9]+$ && "${AUTH_RUN:?}" =~ ^[0-9]+$ ]]
[[ "${RELEASE_TAG:?}" =~ ^v[0-9]+\.[0-9]+-(full|lite)-preview\.[0-9]+$ ]]
[[ "$RELEASE_TAG" == *"-$FLAVOR-preview."* ]]
test "${GH_REPO:?}" = zengyiqian2014-boop/mnOS
work=$(mktemp -d)
cd "$work"
for pair in "$BUILD_RUN:$workflow:build" "$AUTH_RUN:authentication.yml:auth"; do
  IFS=: read -r run path kind <<< "$pair"
  gh api "repos/$GH_REPO/actions/runs/$run" > "$kind.json"
  jq -e --arg repo "$GH_REPO" --arg path ".github/workflows/$path" '
    .repository.full_name == $repo and .head_repository.full_name == $repo and
    .path == $path and .status == "completed" and .conclusion == "success" and
    .event != "pull_request"' "$kind.json" >/dev/null
  gh api "repos/$GH_REPO/actions/runs/$run/jobs?per_page=100" > "$kind-jobs.json"
done
sha=$(jq -r .head_sha build.json)
[[ "$sha" =~ ^[0-9a-f]{40}$ ]]
test "$(jq -r .head_sha auth.json)" = "$sha"
jq -e '[.jobs[] | select(.name | startswith("linux ("))] |
  length == 2 and all(.[]; .conclusion == "success")' auth-jobs.json >/dev/null
jq -e 'any(.jobs[]; .name == "native" and .conclusion == "success")' auth-jobs.json >/dev/null
jq -e --arg prefix "$job_prefix" '[.jobs[] | select(.name | startswith($prefix))] |
  length == 2 and all(.[]; .conclusion == "success")' build-jobs.json >/dev/null
if [ "$FLAVOR" = full ]; then
  jq -e '[.jobs[] | select(.name | startswith("build ("))] | all(.[];
    any(.steps[]; .name == "Verify bundled applications, compiler headers and detailed manuals" and .conclusion == "success") and
    any(.steps[]; .name == "Verify graphical tool center and CTF application windows" and .conclusion == "success") and
    any(.steps[]; .name == "Install to a blank disk and boot without the ISO" and .conclusion == "success"))' build-jobs.json >/dev/null
else
  jq -e '[.jobs[] | select(.name | startswith("linux ("))] | all(.[];
    any(.steps[]; .name == "Boot installer, install, remove ISO, test password login, desktop and C++" and .conclusion == "success"))' build-jobs.json >/dev/null
fi
mkdir release evidence
for arch in amd64 arm64; do
  gh run download "$BUILD_RUN" --repo "$GH_REPO" --name "$artifact_prefix-$arch" --dir "$arch"
  old="mnOS-2.0-preview-installer$suffix-$arch.iso"
  test "$(cat "$arch/source-installer-$arch.txt")" = "$sha"
  (cd "$arch" && sha256sum --check --strict "$old.sha256")
  log="$arch/validation/install-vm.log"
  # The harness also writes separate serial logs. Check actual installed-system
  # markers, not just its exit status or an installer screenshot.
  grep -q '^MNOS_DISK_DESKTOP_PASS' "$arch/validation/disk-virtio.log"
  grep -q '^MNOS_DISK_DESKTOP_PASS' "$arch/validation/disk-nvme.log"
  test -s "$arch/validation/disk-virtio-desktop.ppm"
  test -s "$arch/validation/disk-nvme-desktop.ppm"
  if [ "$arch" = amd64 ]; then test -s "$arch/validation/disk-bios-login.ppm"; fi
  if [ "$FLAVOR" = lite ]; then
    grep -q '^MNOS_LITE_APT_CPP_PASS' "$arch/validation/disk-virtio.log"
  else
    grep -q '^MNOS_APPLICATIONS_PASS' "$arch/validation/disk-virtio.log"
    test -s "$arch/validation/vscode-window.ppm"
    test -s "$arch/validation/ctf-workbench.png"
  fi
  test "$(stat -c %s "$arch/$old")" -le 2147483647
  name="mnOS-${RELEASE_TAG#v}-$arch.iso"
  mv "$arch/$old" "release/$name"
  cp "$arch"/*.tsv "$arch"/*.config "$arch"/*.txt release/
  cp -a "$arch/validation" "evidence/$arch"
done
printf '%s\n' "$sha" > release/SOURCE_COMMIT.txt
{
  printf 'Flavor: %s\nSource: %s\n' "$FLAVOR" "$sha"
  printf 'Installation validation: https://github.com/%s/actions/runs/%s\n' "$GH_REPO" "$BUILD_RUN"
  printf 'Authentication validation: https://github.com/%s/actions/runs/%s\n' "$GH_REPO" "$AUTH_RUN"
  printf 'The published ISO bytes are unchanged from these successful build artifacts.\n'
} > release/PROVENANCE.txt
gh api "repos/$GH_REPO/tarball/$sha" > release/mnos-source.tar.gz
tar -czf release/installation-validation.tar.gz evidence
(cd release && sha256sum -- * > SHA256SUMS && sha256sum --check --strict SHA256SUMS)
cat > notes.md <<EOF
mnOS Linux ${FLAVOR^} 安装预览版，AMD64 / ARM64 各一份完整 ISO。

此版本使用维护中的 Linux 内核；自研内核版另行发布。相同架构的 ISO 同时用于已验证的虚拟机和支持的 UEFI 硬件，并不保证所有设备兼容。

- Full：桌面、Firefox、VS Code、C/C++ 与 CTF 工具，可选密码 + 验证器。支持本机二维码和手动密钥绑定。
- Lite：C++ 会话管理器、桌面组件、C/C++、APT、常用网络工具；不含 Python 运行时，初始登录仅密码。
- 精简版可用 \`sudo mnos full\` 补装完整功能；\`sudo mnos update\` 更新 mnOS 组件，\`sudo mnos rollback 版本\` 回退源中仍保留的组件版本。
- Linux 软件用 \`sudo apt update\`、\`sudo apt install 软件名\`；\`help apt\` 为英文，\`help -cn apt\` 为中文。

Full 可选择附加人脸验证：密码和已开启的 TOTP 之后，通过本地 X11 摄像头窗口或单独的一次性人脸恢复码验证。模板加密保存在 Login Process 目录，识别进程没有 root 权限。普通 RGB 摄像头不是可信 3D 硬件；自动化测试覆盖真实 PAM 和代理，成功识别使用测试进程模拟，尚无实体摄像头防伪测量。
安装时可选择 English / 简体中文；安装后在语言与地区设置修改语言、格式、时区和时间服务器。
Native 的初期 Linux 客体仍不支持 APT、网络或 Linux GUI 应用；本 Linux 版与自研内核版分开发行。
回退命令不回退 Debian 内核、其他软件包或个人数据。Linux 账号需先关闭已启用的 TOTP 和人脸验证，再更换密码并重新绑定。

两种架构均完成真实安装、移除 ISO 后启动、普通用户密码登录、VirtIO/NVMe 磁盘和桌面检查；AMD64 还检查 BIOS 磁盘启动。测试记录、源码、校验和与来源随附件发布。

[安装测试](https://github.com/$GH_REPO/actions/runs/$BUILD_RUN) · [认证测试](https://github.com/$GH_REPO/actions/runs/$AUTH_RUN) · [签名更新资源](https://github.com/$GH_REPO/releases/tag/updates-stable)
EOF
if gh release view "$RELEASE_TAG" --repo "$GH_REPO" --json isDraft,targetCommitish > existing.json 2>/dev/null; then
  jq -e --arg sha "$sha" '.isDraft == true and .targetCommitish == $sha' existing.json >/dev/null
else
  gh release create "$RELEASE_TAG" --repo "$GH_REPO" --target "$sha" --draft --prerelease --latest=false \
    --title "mnOS Linux ${FLAVOR^} ${RELEASE_TAG#v}" --notes-file notes.md
fi
gh release upload "$RELEASE_TAG" --repo "$GH_REPO" --clobber release/*
gh release edit "$RELEASE_TAG" --repo "$GH_REPO" --draft=false --prerelease --latest=false --notes-file notes.md
