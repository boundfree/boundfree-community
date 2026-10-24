---
title: Community 版本与发布 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# Community 版本与发布 SOP

本流程仅适用于本仓库。GitHub `Vsonic12138/boundfree-eeg-native` 统一管理 Issue、PR、代码评审、发版验收和 Actions；Gitee `BigYellow12138/boundfree-community` 是国内源码与 Release 镜像，不另建 Issue/PR。推送 Gitee 本身不会触发 GitHub Actions；合入 GitHub `master` 才会自动构建。两个远端的可见性独立，不因本流程改变。

开始修改、暂存与提交、PR 审查、主线 CI 和 Gitee 同步按 [提交与交付 SOP](contribution-sop.md)。仓库代理入口为 [community-delivery skill](../.agents/skills/community-delivery/SKILL.md)，只引用本流程，不另维护一份发布规则。

## 1. 只记住三个状态

| 面向谁 | 仓库版本 | 安装包版本示例 | 是否自动发布 Release |
| --- | --- | --- | --- |
| 内部开发 | `0.2.2-dev` | `0.2.2-dev.158.1+g12345678` | 否 |
| 小范围试用 | `0.2.2-beta.1` | `0.2.2-beta.1` | 否，维护者选择发版 |
| 正式交付 | `0.2.2` | `0.2.2` | 否，维护者检查草稿后发布 |

`158` 是 Community CI 的运行编号，`1` 是重跑次数，`12345678` 是提交 SHA 前八位。源码不被自动提交回写。普通 CI 一律输出内部 dev 包，即使版本准备 PR 中已填写 beta/stable；只有独立 Release Action 保留 beta/stable 身份并执行相应签名策略。完整 SHA、基础版本和本次运行编号在包内 `build-info.json`，Native、边车与安装器读取同一份生成的 `version.json`。运行编号是工作流标识，不是产品 PATCH，也不是跨工作流统一的发布计数。

普通提交无需修改版本。已发布版本的 bug 修复用 PATCH；新阶段能力用 MINOR；重大不兼容变化评估 MAJOR。`1.0.0` 留给第一个完整、可持续交付产品。历史 `v3.*` tag 来自上游历史，不代表 Community 产品版本；本流程只识别 `community-v*`。

## 2. 日常开发与下载安装包

1. 从更新后的 `master` 切功能分支，提交后向 GitHub `master` 创建 PR。
2. PR 自动运行 `Community CI`：编译 Windows/Linux、版本/i18n/资源检查、Python 与格式契约回归、ICA/导出/播放回归、冻结边车、打包与安装态 smoke。
3. 合入 `master` 后再次自动构建该主线提交。**自动构建不会创建 Release，也不会推送 tag。**
4. GitHub 仓库 → **Actions → Community CI → 成功的运行 → Artifacts**，下载 `BoundFreeEEG-Windows-x64` 或 `BoundFreeEEG-Linux-x64`，解压外层 Artifact ZIP。
5. Windows 运行里面的 `BoundFreeEEG_Setup_*.exe`；Linux 解压 `.tar.gz` 后运行 `BoundFreeEEG/BoundFreeEEG.sh`。
6. 核对旁边 `SHA256SUMS-*.txt` 和 `build-info-*.json`。保存缺陷时附上完整版本和提交 SHA。

源码回归使用开发配置以避免激活弹窗；实际安装包另行使用 `BF_REQUIRE_LICENSE=ON` 和仓库公钥构建，并执行授权门禁 smoke。

本仓库自有核心已采用 Apache-2.0；当前官方包的整机激活尚待迁移为免费核心与扩展授权，许可调整不代表该功能已完成。发布包应包含 `legal/LICENSE.txt`、`legal/NOTICE.txt`、`legal/LICENSING.md` 和第三方声明；前两份分别与源树根目录 `LICENSE`、`NOTICE` 一致，发布资产检查负责校验。许可与未来扩展交付边界见 [说明](../legal/LICENSING.md)。

Qt 的 LGPL v3 与其引用的 GPL v3 完整正文位于 `legal/licenses/`，静态检查会拒绝缺失或被摘要替代的正文。发版前还需核对实际 Windows/Linux 包中的全部第三方许可副本、归属与对应源码获取信息；声明文件或静态检查通过不表示已完成该项验收。

Artifacts 默认保留 30 天，过期可从同一代码提交重新构建（开发包重跑次数变化）。值得长期保留的试用版应走 beta Release。GitHub 当前仓库访问权限同样限制下载权限；本流程不自动公开仓库。

手动重建：**Actions → Community CI → Run workflow**，选分支。按钮要求工作流已在默认分支存在；首次接入在 PR 上验证，合入后按钮才可正常使用。

## 3. 调整交付阶段版本

在功能分支上执行，示例仅表示操作方式，不表示当前版本已验收：

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

同步工具更新 `version.json`、`version.md`、`README.md`、`AGENTS.md`、`installer/BoundFreeEEG.iss` 五处，检查 diff 后一起提交。不要手动只改 JSON。`ipc_protocol_version` 不随产品版本自动递增；边车 Python 包自身的 setuptools 版本属于内部实现，运行时产品版本仍以 `version.json` 为准。

## 4. 创建 beta / stable Release 草稿

1. 在本仓库 **Issues → New issue → Community 发版验收** 创建一次发版的验收记录，填入目标版本、变更范围、目标系统、验证结果和限制。
2. 把版本准备 PR 合入 GitHub `master`，等待该提交的 CI 全部成功。确认这份源代码就是要交付的版本。
3. 在该提交创建 annotated tag（以下以 beta 为例）：

```bash
git fetch origin master --tags
git tag -a community-v0.2.2-beta.1 <已通过CI的完整提交SHA> -m "Community 0.2.2-beta.1"
git push origin refs/tags/community-v0.2.2-beta.1
```

4. **Actions → Community Release → Run workflow**，从 `master` 执行，输入已有 tag 和本仓库验收 Issue 的完整 URL。stable 还需确认人工验收已完成。
5. 工作流校验 annotated tag、tag 与 JSON 一致、提交属于 `master` 历史、Issue 属于本仓库且可读取、目标 Release 不存在。通过后重新构建该 tag 的源码、执行测试、安装态 smoke 并上传双平台产物。
6. 两个平台都成功后，在 **Releases** 生成草稿。beta 带 prerelease 标记；stable 不带。检查包名、版本、SHA256、变更说明与验收 Issue，再由维护者点击 **Publish release**。
7. GitHub 发布后，必须按第 7 节把同一 tag、说明和国内安装包下载同步到 Gitee，回下载校验后把两边 URL 与结果写回同一个 GitHub 验收 Issue。当前此步骤由维护者执行，现有 Action 仍只创建 GitHub 草稿；普通 Git 推送不会同步 Release。

草稿页面 URL 可能含 `untagged-*`，产品 tag 以 Release 元数据中的 `tag_name` 为准。通过 API 编辑草稿说明时显式保留目标 `tag_name`、`draft=true` 和 prerelease 状态，编辑后再次核对；不要把草稿页面 URL 当作 Git tag。

不得给 `dev` 创建 Release。tag 创建本身不会自动发布。手动 `Community CI` 也不会创建草稿。Release 构建从精确提交重建，不从另一个 CI 随意取包；草稿引用的是自己的构建运行与 SHA256。

## 5. 对外发布门槛

beta：明确试用范围与已知限制；关键打开/播放/分析/导出路径通过回归；至少在实际目标 Windows 或 Linux 桌面试运行。unsigned beta 可用于试用，必须在说明中明确签名状态。

官方安装包采用**验收并发布后允许公开下载、使用仍需有效授权激活**的方案。`BF_REQUIRE_LICENSE=ON`、现有授权兼容与签名策略保持不变；免费核心免激活和扩展权益属于后续阶段，不作为本轮已经实现的能力。

在发版验收 Issue 和两边 Release 说明中写明试用范围、国内下载步骤、激活要求、签名状态及已知限制。公开下载不等于授予软件激活权益，也不改变 Apache-2.0 源码权利。未验收候选包保持 GitHub 草稿，不提前在公开 Gitee 创建发行版。GitHub 当前私有，下载及 Issue/PR 仍需相应仓库访问权限；Gitee 国内下载必须实际可用，不能只放一个私有 GitHub 链接。本流程不自动邀请协作者或更改可见性。

stable：完成承诺功能验收，无已知数据丢失、错误结果和高频崩溃；完成干净 Windows 10/11 安装、旧版升级、卸载、桌面操作、正式授权兼容与代表性科研文件验证；发布 Linux 时另需目标 Linux 桌面验收。把证据放入本次验收 Issue。runner 的命令行和安装测试不等同于完整干净机、真实 GPU、真实硬件或算法效果验收。

正式 Windows 包继续强制 Authenticode 签名。配置仓库 Actions Secrets：

| Secret | 用途 |
| --- | --- |
| `BF_CODE_SIGN_PFX_BASE64` | 可在 CI 使用的组织代码签名 PFX 的 base64 编码 |
| `BF_CODE_SIGN_PASSWORD` | PFX 密码 |

这是 Windows 软件签名证书，与授权系统签发私钥无关。流水线仅使用仓库已有 `license_public.pem` 嵌入授权校验公钥；绝不需要授权签发私钥。签名证书缺失时 stable 会失败，不允许绕过为 unsigned stable。受硬件保护且不可导出的签名证书需另接组织签名服务，不能直接使用此 PFX 入口。

## 6. 失败、重跑与版本不可变

- PR / master CI 失败：打开失败 step，修复后提交；可用 Re-run all jobs 排查临时网络问题并保持双平台尝试编号一致。测试报告在 `reports-*` Artifact。
- 依赖或网络失败：可以重跑；开发包生成新的尝试编号，不覆盖旧包。依赖直接版本已固定，runner 镜像和传递依赖仍可能变化，完整可复现环境尚需锁定。
- Release 构建失败且尚无草稿：若是环境问题，在同一 tag 上重跑；若需修改源码，创建新 beta.N 或新 PATCH，不移动 tag。
- 已有草稿或已发布 Release：预检分页列出 Release 并按 tag 拦截，编译前拒绝覆盖。按 tag 查询可能对草稿返回 404，只读 token 的列表也可能隐藏草稿，不能据此判断该版本不存在。使用本工作流配置的凭据；接口失败时停止。改用新版本；不替换已经对外分发的同版本安装包。
- 默认和构建任务使用只读 token；仅手动 Release 中的预检与草稿 job 有 `contents: write`。GitHub 要求 push 权限才返回草稿，因此预检需要该配置，脚本只读取数据、不写入仓库或 Release；预检 job 仅在 `master` 执行，其他分支跳过后续任务。配置回归约束此权限及分支范围，不使用仓库用户权限字段推断 GitHub 自动 token 的实际授权；最终以 runner 能枚举并拒绝已有草稿验收。PR 构建不会获得签名 secrets。
- Windows 下载的是 Setup 安装包；Linux 当前为 **Ubuntu 24.04 x86_64** 归档，含 Qt、冻结边车和主要动态库，不承诺其他发行版或 AppImage 兼容。仍依赖目标桌面、驱动和系统服务。

## 7. 同步 Gitee Release 与国内下载

GitHub 是发布与验收记录的来源，Gitee 是国内镜像；同一产品版本只对应同一个 annotated tag、源提交和原始安装包 SHA256。GitHub 草稿不触发 Gitee 发布。Gitee 创建发行版会对仓库访客可见，本流程不依赖 CLI 的 `--draft` 隐藏它。

### 7.1 准备同一份安装包

[Gitee 官方说明](https://help.gitee.com/repository/release/create)规定普通项目单个附件最多 100M、仓库附件总容量 1G（不同项目资质有不同额度）。当前 Windows/Linux 原包均超出普通单附件额度，默认使用 **90,000,000 字节分卷**，不另行编译、不修改安装器，也不需要对象存储。发布前核对仓库剩余附件容量；容量不足时停止并记录，不静默删除历史版本或退化成只有 GitHub 链接。

维护者先确认该版本已通过目标阶段验收、GitHub Release 已发布，并检查发布说明。下面示例从仓库根目录执行，临时目录放在工作空间 `build/`，已有目录不覆盖：

```bash
COMMUNITY_TAG=community-v0.2.2-beta.1
COMMUNITY_MIRROR_DIR=../build/community-gitee-beta1
# 读取已发布的 Release 元数据；草稿、404 或接口错误时停止。
gh api "repos/Vsonic12138/boundfree-eeg-native/releases/tags/$COMMUNITY_TAG"
# 人工确认 tag_name、draft=false、published_at、版本和附件完整后继续。
gh release download "$COMMUNITY_TAG" --repo Vsonic12138/boundfree-eeg-native \
  --dir "$COMMUNITY_MIRROR_DIR/source"
python3 scripts/common/prepare_gitee_release.py \
  --windows "$COMMUNITY_MIRROR_DIR/source" --linux "$COMMUNITY_MIRROR_DIR/source" \
  --output "$COMMUNITY_MIRROR_DIR/gitee"
```

准备工具验证两平台均为同一 Release 的 beta/stable 版本、源 SHA、tag、构建运行及原包/构建信息 SHA256；拒绝普通 CI dev 包、缺失校验、混用构建或覆盖输出目录。输出 `mirror-plan.json` 供维护者核对，以及 `Windows/`、`Linux/` 两组分卷、还原脚本、原始构建信息和校验文件。它只写本地文件，不创建 tag、Release 或远端附件；可在候选包阶段预演，预演成功不代表允许发布。

### 7.2 同步 tag、说明与附件

1. 按提交 SOP 同步已验证源码。确认目标 annotated tag 的对象 SHA 和 peeled commit 在两个远端相同；同步已有 tag，不能依赖 Gitee 自动在默认分支创建新 tag：

```bash
git fetch origin master --tags
COMMUNITY_SHA=$(git rev-parse "$COMMUNITY_TAG^{commit}")
git push gitee "refs/tags/$COMMUNITY_TAG:refs/tags/$COMMUNITY_TAG"
git ls-remote origin "refs/tags/$COMMUNITY_TAG" "refs/tags/$COMMUNITY_TAG^{}"
git ls-remote gitee "refs/tags/$COMMUNITY_TAG" "refs/tags/$COMMUNITY_TAG^{}"
```

2. 用 `ge release list --repo BigYellow12138/boundfree-community` 核对目标版本。任何认证/网络错误都停止；已存在时进入补齐核验，不盲目创建、删除或覆盖。发布说明保留同一变更范围、已知限制、激活与签名状态，增加下述国内还原步骤、每个平台原包 SHA256，以及 GitHub Issue/PR 入口。GitHub 仍私有时明确其访问条件。Gitee 公开下载的新安排取代早期候选包说明中的“仅定向交付”，不会改变随包 Apache-2.0 或第三方许可。
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

先通过真实 GitHub PR 运行验证构建、测试与产物。合入默认分支后再验证手动 CI 和 Release 入口。未经实际运行的入口必须在验收报告注明“待验证”；不能为了演练直接发布正式版或把开发版本改成稳定版。首次实施快照见 [归档记录](archive/reports/community-release-automation.md)，流程验证及后续证据见 [Issue #2](https://github.com/Vsonic12138/boundfree-eeg-native/issues/2)；当前待验收项以 [清单](feature-parity-checklist.md) 为准。

参考：[GitHub 触发事件](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)、[Artifacts](https://docs.github.com/en/actions/tutorials/store-and-share-data)、[SemVer](https://semver.org/)。
