#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Publish one signed catalog and a categorized download page in this repository.
set -euo pipefail
repo=${GH_REPO:?}; key=${1:?signing key fingerprint}; version=${2:?latest runtime version}
[[ "$version" =~ ^[0-9][a-zA-Z0-9.+~-]*$ ]]
test "$repo" = zengyiqian2014-boop/mnOS
work=$(mktemp -d)
gh api "repos/$repo/releases?per_page=100" > "$work/releases.json"
jq --arg version "$version" --arg updated "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '
  {schema:1, latest_linux_runtime:$version, updated:$updated,
   resources:[.[] | select(.draft == false) | . as $release |
     select(.tag_name == "updates-stable" or (.tag_name | test("-(full|lite|linux|native)-preview\\."))) |
     .assets[] | select(.name | test("\\.(iso|deb)$")) |
     select((.digest // "") | test("^sha256:[0-9a-f]{64}$")) |
     {name:.name, url:.browser_download_url, bytes:.size, sha256:(.digest | sub("^sha256:";"")),
      kind:(if (.name|endswith(".deb")) then "patch" elif (.name|contains("-lite-")) then "lite" else "full" end),
      edition:(if (.name|contains("native")) then "native" else "linux" end),
      architecture:(if (.name|test("(amd64|x86_64)")) then "amd64" elif (.name|contains("arm64")) then "arm64" else "all" end),
      version:(if (.name|endswith(".deb")) then (.name|split("_")[1]) else $release.tag_name end),
      release:$release.html_url}] | sort_by(.kind,.edition,.architecture,.version,.name)}' \
  "$work/releases.json" > "$work/catalog.json"
for arch in amd64 arm64; do
  jq -e --arg arch "$arch" --arg version "$version" 'any(.resources[];
    .name == ("mnos-runtime_"+$version+"_"+$arch+".deb"))' "$work/catalog.json" >/dev/null
done
gpg --batch --yes --local-user "$key" --clearsign --digest-algo SHA256 \
    -o "$work/catalog.json.asc" "$work/catalog.json"
gpgv --keyring "$PWD/live/keys/mnos-archive.gpg" "$work/catalog.json.asc"
cat > "$work/README.md" <<'EOF'
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
EOF
for kind in full lite patch; do
  case "$kind" in full) title='完整版 / Full';; lite) title='精简版 / Lite';; patch) title='补丁与功能包 / Packages';; esac
  printf '\n## %s\n\n| 系统 | 架构 | 版本 | 下载 |\n| --- | --- | --- | --- |\n' "$title" >> "$work/README.md"
  jq -r --arg kind "$kind" '.resources[] | select(.kind==$kind) |
    "| \(.edition) | \(.architecture) | \(.version) | [\(.name)](\(.url)) |"' "$work/catalog.json" >> "$work/README.md"
done
# Git data API creates a single atomic branch commit. No second remote repository
# and no plaintext signing key is included in any blob or commit.
parent=''
if gh api "repos/$repo/git/ref/heads/resources" > "$work/ref.json" 2>/dev/null; then parent=$(jq -r .object.sha "$work/ref.json"); fi
printf '[]\n' > "$work/tree.json"
for name in README.md catalog.json catalog.json.asc; do
  jq -n --rawfile content "$work/$name" '{content:$content,encoding:"utf-8"}' > "$work/blob.json"
  blob=$(gh api --method POST "repos/$repo/git/blobs" --input "$work/blob.json" --jq .sha)
  jq --arg name "$name" --arg sha "$blob" '.+[{path:$name,mode:"100644",type:"blob",sha:$sha}]' "$work/tree.json" > "$work/tree.next"
  mv "$work/tree.next" "$work/tree.json"
done
jq '{tree:.}' "$work/tree.json" > "$work/tree-request.json"
tree=$(gh api --method POST "repos/$repo/git/trees" --input "$work/tree-request.json" --jq .sha)
jq -n --arg tree "$tree" --arg parent "$parent" --arg version "$version" \
  '{message:("Update signed mnOS resource catalog to "+$version),tree:$tree,
    parents:(if $parent=="" then [] else [$parent] end),
    author:{name:"zengyiqian2014-boop",email:"235238542+zengyiqian2014-boop@users.noreply.github.com"},
    committer:{name:"zengyiqian2014-boop",email:"235238542+zengyiqian2014-boop@users.noreply.github.com"}}' > "$work/commit.json"
commit=$(gh api --method POST "repos/$repo/git/commits" --input "$work/commit.json" --jq .sha)
if [ -n "$parent" ]; then
  jq -n --arg sha "$commit" '{sha:$sha,force:false}' > "$work/update.json"
  gh api --method PATCH "repos/$repo/git/refs/heads/resources" --input "$work/update.json" >/dev/null
else
  jq -n --arg sha "$commit" '{ref:"refs/heads/resources",sha:$sha}' > "$work/update.json"
  gh api --method POST "repos/$repo/git/refs" --input "$work/update.json" >/dev/null
fi
gh release upload updates-stable --repo "$repo" "$work/catalog.json.asc" "$work/catalog.json" --clobber
printf 'Catalog: https://github.com/%s/tree/resources\nCommit: %s\n' "$repo" "$commit"
