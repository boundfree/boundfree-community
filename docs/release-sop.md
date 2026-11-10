---
title: Community 版本与发布 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-25
---

# Community 版本与发布 SOP

本文负责产品版本、安装包核验、GitHub Release 和 Gitee Release 镜像。研发协作、远端权限、PR、主线 CI 和 Gitee 源码同步统一按 [提交与交付 SOP](contribution-sop.md)，这里不再重复其操作。

## 0. 从开发包到双平台发布

发版顺序：**调整阶段版本（第 3 节）→ 经提交 SOP 合入并验证主线 → 选定源 SHA 创建 tag/草稿（第 4 节）→ 验收并 Publish（第 5 节）→ Gitee Release 镜像核验（第 7 节）**。

维护者选版本、触发 `Community Release`；Action 从精确 tag 重建、测试并生成 GitHub 草稿。Publish 和 Gitee 镜像由维护者执行，目前没有自动镜像 Action。GitHub 已发布但 Gitee 附件未验证时，记录“Gitee 镜像待完成”；草稿阶段不要求勾选发布后的镜像验收。

这里的双平台指 GitHub/Gitee；Windows/Linux 是安装包目标系统。

## 1. 只记住三个状态

| 交付阶段 | 源码版本 | 安装包版本示例 |
| --- | --- | --- |
| 内部开发 | `0.2.2-dev` | `0.2.2-dev.158.1+g12345678` |
| 小范围试用 | `0.2.2-beta.1` | `0.2.2-beta.1` |
| 正式交付 | `0.2.2` | `0.2.2` |

`158` 是 Community CI 的运行编号，`1` 是重跑次数，`12345678` 是提交 SHA 前八位。源码不被自动提交回写。普通 CI 一律输出内部 dev 包，即使版本准备 PR 中已填写 beta/stable；只有独立 Release Action 保留 beta/stable 身份并执行相应签名策略。完整 SHA、基础版本和本次运行编号在包内 `build-info.json`，Native、边车与安装器读取同一份生成的 `version.json`。运行编号是工作流标识，不是产品 PATCH，也不是跨工作流统一的发布计数。

`version.json` 是产品版本真值；`version.md` 记录阶段变化，普通提交由 Git 追溯，无需改版本。已发布版本修复用 PATCH，新阶段能力用 MINOR，重大不兼容变化评估 MAJOR；`0.x` 表示产品仍在成型，首个完整、可持续交付产品再考虑 `1.0.0`。`alpha/rc` 仅保留历史兼容，不作为默认流程。

产品版本各数值段不得超过 Windows FileVersion 的 65535；Win32 四段数值为基础版本加 `.0`，完整 SemVer 才标识安装包。`ipc_protocol_version` 独立维护，协议不兼容时另行评估。历史 `v3.*` tag 不代表 Community 产品版本；本流程只识别 `community-v*`。

## 2. 安装包下载与核验

内部包：GitHub **Actions → 对应成功运行 → Artifacts**，下载 `BoundFreeEEG-Windows-x64` 或 `BoundFreeEEG-Linux-x64`，解压外层 ZIP。Artifacts 默认保留 30 天，过期后的重建按提交 SOP 手动触发 CI。发布候选包则从该版本 GitHub Release 草稿/附件获取；长期保存的试用包应走 beta 发布。

每次交付都核对：包旁 `SHA256SUMS-*.txt` 与实际文件一致，`build-info-*.json` 的完整源 SHA、运行 URL、版本及 release 标志对应目标构建，包内元数据一致。不能混用 PR、主线和独立 Release 的产物；缺陷记录附完整版本与 SHA。

Windows 运行 `BoundFreeEEG_Setup_*.exe`；Linux 当前仅支持 **Ubuntu 24.04 x86_64**，解压 `.tar.gz` 后运行 `BoundFreeEEG/BoundFreeEEG.sh`，仍依赖目标桌面、驱动及系统服务，不承诺其他发行版或 AppImage 兼容。下载受仓库访问权限约束。

## 3. 调整交付阶段版本

在任务分支上执行，版本准备通常使用 `chore/*`，按 [分支规范](development-guide.md#branch-model) 经 PR 合入，不另建长期 `release` 分支。示例仅表示操作方式，不表示当前版本已验收：

```bash
# 选择一个开发阶段给试用者
python3 scripts/common/version_sync.py set 0.2.2-beta.1 --type chore --summary "准备首轮试用"
# beta 修复后再发一轮
python3 scripts/common/version_sync.py set 0.2.2-beta.2 --type chore --summary "修复试用反馈并准备第二轮"
# 验收完成，准备正式版
python3 scripts/common/version_sync.py set 0.2.2 --type chore --summary "完成验收并准备正式发布"
# 正式版发布后，开启下一修复周期
python3 scripts/common/version_sync.py next patch --type chore --summary "开启下一修复周期"
# 或开启下一功能阶段，二选一
python3 scripts/common/version_sync.py next minor --type chore --summary "开启下一功能阶段"
python3 scripts/common/version_sync.py check
```

同步工具更新 `version.json`、`version.md`、`README.md`、`AGENTS.md`、`installer/BoundFreeEEG.iss` 五处，检查 diff 后按提交 SOP 一起交付，不手动只改 JSON。边车 setuptools 版本是内部实现，不作为产品版本。

## 4. 创建 beta / stable Release 草稿

1. 在 **GitHub 本仓库 → Issues → New issue → Community 发版验收** 创建一次发版的验收记录，填入目标版本、变更范围、目标系统、验证结果和限制。
2. 确认第 3 节的版本准备已按提交 SOP 完成交付，选定已通过主线 CI 的完整 SHA。
3. 在该提交创建 annotated tag（以下以 beta 为例）：

```bash
git fetch origin master --tags
git tag -a community-v0.2.2-beta.1 <已通过CI的完整提交SHA> -m "Community 0.2.2-beta.1"
git push origin refs/tags/community-v0.2.2-beta.1
```

4. **Actions → Community Release → Run workflow**，从 `master` 执行，输入已有 tag 和本仓库验收 Issue 的完整 URL。stable 还需确认人工验收已完成。
5. 工作流校验 annotated tag、tag 与 JSON 一致、提交属于 `master` 历史、Issue 属于本仓库且可读取、目标 Release 不存在。通过后重新构建该 tag 的源码、执行测试、安装态 smoke 并上传双平台产物。
6. 双目标系统构建均成功后生成草稿，beta 带 prerelease 标记，stable 不带。按第 2 节核验包，并完成第 5 节验收后由维护者点击 **Publish release**；随后执行第 7 节。

草稿页面 URL 可能含 `untagged-*`，产品 tag 以 Release 元数据中的 `tag_name` 为准。通过 API 编辑草稿说明时显式保留目标 `tag_name`、`draft=true` 和 prerelease 状态，编辑后再次核对；不要把草稿页面 URL 当作 Git tag。

不得给 dev 创建 Release，也不把日常 CI 改称发布包。发布后的主线可继续开发，但已选 tag 固定在原始源 SHA，不随 `master` 移动。

## 5. 对外发布门槛

beta：明确试用范围与已知限制；关键打开/播放/分析/导出路径通过回归；至少在实际目标 Windows 或 Linux 桌面试运行。unsigned beta 可用于试用，必须在说明中明确签名状态。

官方安装包采用**验收发布后可公开下载、使用仍需有效授权激活**的方案，以 `BF_REQUIRE_LICENSE=ON` 和仓库公钥构建，并验证授权门禁；源码回归使用开发配置。免费核心免激活与扩展权益仍待实现，许可及扩展边界见 [LICENSING.md](../legal/LICENSING.md)。

发版验收 Issue 和两边说明须写明范围、国内下载步骤、激活/签名状态及已知限制。公开下载不增加激活权益或限制 Apache-2.0 源码权利。未验收候选包保持 GitHub 草稿，不提前在 Gitee 公开。

随包 `legal/LICENSE.txt` / `NOTICE.txt` 应与根目录 `LICENSE` / `NOTICE` 一致，并包含许可边界与第三方声明。Qt LGPL/GPL 完整正文位于 `legal/licenses/`；逐包核对全部实际依赖的许可正文、归属、对应源码获取与重链接事项，静态资产检查不能代替二进制分发核验。

stable：完成承诺功能验收，无已知数据丢失、错误结果和高频崩溃；完成干净 Windows 10/11 安装、旧版升级、卸载、桌面操作、正式授权兼容与代表性科研文件验证；发布 Linux 时另需目标 Linux 桌面验收。把证据放入本次验收 Issue。runner 的命令行和安装测试不等同于完整干净机、真实 GPU、真实硬件或算法效果验收。

正式 Windows 包继续强制 Authenticode 签名。配置仓库 Actions Secrets：

| Secret | 用途 |
| --- | --- |
| `BF_CODE_SIGN_PFX_BASE64` | 可在 CI 使用的组织代码签名 PFX 的 base64 编码 |
| `BF_CODE_SIGN_PASSWORD` | PFX 密码 |

这是 Windows 软件签名证书，与授权系统签发私钥无关。流水线仅使用仓库已有 `license_public.pem` 嵌入授权校验公钥；绝不需要授权签发私钥。签名证书缺失时 stable 会失败，不允许绕过为 unsigned stable。受硬件保护且不可导出的签名证书需另接组织签名服务，不能直接使用此 PFX 入口。

## 6. 失败、重跑与版本不可变

- 日常 PR / master CI 的定位和重跑按 [提交 SOP 第 2.4 节](contribution-sop.md#24-评审与合入)。
- Release 构建失败且尚无草稿：若是环境问题，在同一 tag 上重跑；若需修改源码，创建新 beta.N 或新 PATCH，不移动 tag。直接依赖版本已固定，但 runner 和传递依赖仍可能变化，不承诺逐字节可复现。
- 已有草稿或已发布 Release：预检分页列出 Release 并按 tag 拦截，编译前拒绝覆盖。按 tag 查询可能对草稿返回 404，只读 token 的列表也可能隐藏草稿，不能据此判断该版本不存在。使用本工作流配置的凭据；接口失败时停止。改用新版本；不替换已经对外分发的同版本安装包。
- 默认和构建任务使用只读 token；仅手动 Release 中的预检与草稿 job 有 `contents: write`。GitHub 要求 push 权限才返回草稿，因此预检需要该配置，脚本只读取数据、不写入仓库或 Release；预检 job 仅在 `master` 执行，其他分支跳过后续任务。配置回归约束此权限及分支范围，不使用仓库用户权限字段推断 GitHub 自动 token 的实际授权；最终以 runner 能枚举并拒绝已有草稿验收。

## 7. 同步 Gitee Release 与国内下载

GitHub 是发布与验收记录的来源，Gitee 是国内镜像；同一产品版本只对应同一个 annotated tag、源提交和原始安装包 SHA256。GitHub 草稿不触发 Gitee 发布。Gitee 创建发行版会对仓库访客可见，本流程不依赖 CLI 的 `--draft` 隐藏它。

### 7.1 准备同一份安装包

[Gitee 官方说明](https://help.gitee.com/repository/release/create)规定普通项目单个附件最多 100M、仓库附件总容量 1G（不同项目资质有不同额度）。当前 Windows/Linux 原包均超出普通单附件额度，默认使用 **90,000,000 字节分卷**，不另行编译、不修改安装器，也不需要对象存储。发布前核对仓库剩余附件容量；容量不足时停止并记录，不静默删除历史版本或退化成只有 GitHub 链接。

维护者先确认该版本已通过目标阶段验收、GitHub Release 已发布，并检查发布说明。下面示例从仓库根目录执行，临时目录放在工作空间 `build/`，已有目录不覆盖：

```bash
COMMUNITY_TAG=community-v0.2.2-beta.1
COMMUNITY_MIRROR_DIR=../build/community-gitee-beta1
# 读取已发布的 Release 元数据；草稿、404 或接口错误时停止。
gh api "repos/boundfree/boundfree-community/releases/tags/$COMMUNITY_TAG"
# 人工确认 tag_name、draft=false、published_at、版本和附件完整后继续。
gh release download "$COMMUNITY_TAG" --repo boundfree/boundfree-community \
  --dir "$COMMUNITY_MIRROR_DIR/source"
python3 scripts/common/prepare_gitee_release.py \
  --windows "$COMMUNITY_MIRROR_DIR/source" --linux "$COMMUNITY_MIRROR_DIR/source" \
  --output "$COMMUNITY_MIRROR_DIR/gitee"
```

迁入组织后的构建运行地址使用 `boundfree/boundfree-community`；旧候选包内不可变的 `Vsonic12138/boundfree-eeg-native` 运行地址仍被准备工具接受，不重写历史构建信息或附件。

准备工具验证两平台均为同一 Release 的 beta/stable 版本、源 SHA、tag、构建运行及原包/构建信息 SHA256；拒绝普通 CI dev 包、缺失校验、混用构建或覆盖输出目录。输出 `mirror-plan.json` 供维护者核对，以及 `Windows/`、`Linux/` 两组分卷、还原脚本、原始构建信息和校验文件。它只写本地文件，不创建 tag、Release 或远端附件；可在候选包阶段预演，预演成功不代表允许发布。

### 7.2 同步 tag、说明与附件

1. 源码同步按 [提交 SOP 第 2.6 节](contribution-sop.md#26-同步-gitee) 完成后，只在此同步发布 tag，核对两端 tag 对象和 peeled commit 相同；不让 Gitee 在默认分支自动创建替代 tag：

```bash
git fetch origin master --tags
COMMUNITY_SHA=$(git rev-parse "$COMMUNITY_TAG^{commit}")
git push gitee "refs/tags/$COMMUNITY_TAG:refs/tags/$COMMUNITY_TAG"
git ls-remote origin "refs/tags/$COMMUNITY_TAG" "refs/tags/$COMMUNITY_TAG^{}"
git ls-remote gitee "refs/tags/$COMMUNITY_TAG" "refs/tags/$COMMUNITY_TAG^{}"
```

2. 用 `ge release list --repo BigYellow12138/boundfree-community` 核对目标版本。任何认证/网络错误都停止；已存在时进入补齐核验，不盲目创建、删除或覆盖。发布说明保留同一变更范围、已知限制、激活与签名状态，增加下述国内还原步骤、每个平台原包 SHA256，以及 GitHub Issue/PR 入口。旧候选包若写“仅定向交付”，在本次发布说明中澄清当前安排；不改变原包许可或内容。
3. 把核对完成的说明写入 `$COMMUNITY_MIRROR_DIR/gitee-notes.md`。该文件还应列出预期分卷数量，上传未完成期间标明“附件同步中”。确认 GitHub 已发布且材料齐全后创建 Gitee 发行版并上传：

```bash
# beta 示例；stable 时省略 --prerelease。
ge release create "$COMMUNITY_TAG" --repo BigYellow12138/boundfree-community \
  --target "$COMMUNITY_SHA" --title "Community ${COMMUNITY_TAG#community-v}" \
  --prerelease --notes-file "$COMMUNITY_MIRROR_DIR/gitee-notes.md"
ge release upload "$COMMUNITY_TAG" --repo BigYellow12138/boundfree-community \
  "$COMMUNITY_MIRROR_DIR"/gitee/Windows/* "$COMMUNITY_MIRROR_DIR"/gitee/Linux/*
```

4. 使用新目录回下载实际 Gitee 附件，核对 `SHA256SUMS-Gitee-*.txt`，运行目标平台还原脚本，并核对还原原包 SHA256 与 GitHub 原始 `SHA256SUMS-*.txt` 一致。Windows/Linux 目标平台各自验证；只做本地字节拼接不能声称 Windows 脚本已运行。核对免维护者凭据的访客下载路径能正常到达 Gitee 附件（平台可能要求登录或验证码），不能用维护者下载成功代替公众下载检查。
5. 在 Gitee 页面确认附件完整后去掉“附件同步中”，在 **同一个 GitHub 发版验收 Issue** 记录 GitHub/Gitee Release URL、tag 对象/源 SHA、原包与分卷校验和实际下载/还原结果。到此才称“两边 Release 已同步”。

```bash
ge release download "$COMMUNITY_TAG" --repo BigYellow12138/boundfree-community \
  --dir "$COMMUNITY_MIRROR_DIR/download-check"
```

若上传中断，保留现有 Release 和已上传附件；根据 `mirror-plan.json` 及回下载校验只补传缺失文件，不用 `--clobber`。同名附件校验不符时停止并调查，不覆盖；包内容需改动时发布新 beta.N/PATCH。重试 Gitee 同步不重建 GitHub 原包，不新建第二份验收 Issue，也不影响日常 CI。

### 7.3 给用户的国内下载说明

- **Windows**：下载该版本的全部 `*.exe.partNNN`、`Restore-Windows.cmd`、`SHA256SUMS-Gitee-Windows.txt` 与原始 `SHA256SUMS-Windows.txt` / `build-info-Windows.json`，放在同一文件夹。在该文件夹打开命令提示符，运行 `Restore-Windows.cmd`；脚本使用系统 Windows PowerShell 校验每卷、还原并校验完整安装包，成功输出 `Verified` 后再运行生成的 `.exe`。不需要 Python 或第三方解压软件。
- **Ubuntu 24.04**：下载全部 `*.tar.gz.partNNN` 及 Linux 的还原/校验/构建信息文件，放在同一文件夹，运行 `sh Restore-Linux.sh`；成功后按第 2 节解压原始归档。脚本依赖系统 `sh`、`sha256sum` 等基础命令。
- 只下载 `.part001` 不能安装。缺卷、损坏、原包校验不符会停止；已有同名输出或 `.partial` 不被覆盖。先保留和检查已有文件再重试，不直接运行分卷，也不把分卷 SHA256 当作完整安装包 SHA256。

## 8. 首次接入的验证边界

先通过真实 GitHub PR 运行验证构建、测试与产物。合入默认分支后再验证手动 CI 和 Release 入口。未经实际运行的入口必须在验收报告注明“待验证”；不能为了演练直接发布正式版或把开发版本改成稳定版。首次实施快照见 Git 历史（提交 `1fe5cec` 的 `docs/archive/reports/community-release-automation.md`），流程验证及后续证据见 [Issue #2](https://github.com/boundfree/boundfree-community/issues/2)；当前待验收项以 [清单](feature-parity-checklist.md) 为准。

参考：[GitHub 触发事件](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)、[Artifacts](https://docs.github.com/en/actions/tutorials/store-and-share-data)、[SemVer](https://semver.org/)。
