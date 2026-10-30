---
title: Community 提交与交付 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-25
---

# Community 提交与交付 SOP

本文负责日常开发交付：明确任务与验收 → 新建分支 → 实现与验证 → 提交 PR → 评审与返修 → 合入与主线包核对 → Gitee 源码同步 → 清理分支。分支约束、提交格式和文档要求见 [开发规范](development-guide.md)；涉及产品版本、tag 或对外发版时，从第 6 节交接到发布 SOP。

新增功能通常按 **Issue → `feature/*` → Draft PR（可选）→ Ready for review → 评审/返修 → merge commit → 主线 CI → 镜像同步 → 分支清理** 推进。CI 成功不代替评审，PR 合入不等于 Release 已发布。

## 0. 两个托管地址与协作顺序

GitHub 与 Gitee 托管的是**同一个 Community 项目**，共用 `master`、版本文件、提交历史与 `community-v*` tag。GitHub 是研发与发布记录的来源，Gitee 是国内镜像；不是各自维护一套功能的两个开发仓库。本流程不适用于另一个产品 `boundfree-pro`。

| 项目 | GitHub `origin` | Gitee `gitee` |
| --- | --- | --- |
| 仓库 | `boundfree/boundfree-community` | `BigYellow12138/boundfree-community` |
| 当前可见性 | 私有，需仓库访问权限 | 公开 |
| Issue、PR、评审与验收 | 唯一协作入口 | 不新建、不复制，不做另一轮合并 |
| 开发与集成 | 短分支 → PR → `master` | 同步 GitHub 已验证的主线，避免直接开发 |
| 构建 | GitHub Actions 自动执行 | 不承担第二套构建 |
| 发布 | 验收记录、原始构建、草稿与首次发布 | 同 tag、源提交和说明，提供国内下载镜像 |
| 文档 | 随代码在 GitHub PR 中更新 | 随主线同步，不单独在线编辑 |

**同步方向：GitHub → Gitee。** 本文第 5 节同步源码；tag 和 Release 的同步归发布 SOP 管理。

组织管理员从 [People](https://github.com/orgs/boundfree/people) → **Invite member** 搜索 GitHub 用户名或邮箱，以 **Member** 邀请；对方接受邀请后加入组织。组织当前默认仓库权限为 **Read**，成员可读取私有仓库；参与开发时，再到本仓库 **Settings → Collaborators and teams** 为成员或团队授予 **Write**。只参与单个仓库的朋友也可在该页面作为外部协作者邀请，不必授予组织 Owner。管理员应在邀请时核对组织默认权限和具体仓库权限。

新成员先取得 GitHub 协作权限，再在其 [Issues](https://github.com/boundfree/boundfree-community/issues) / [Pull requests](https://github.com/boundfree/boundfree-community/pulls) 工作。未获权限时页面可能显示 404，应联系仓库维护者申请访问，不在 Gitee 建立替代任务库。本流程不自动公开 GitHub 或邀请协作者。

新克隆可二选一；以下只用于新目录，已有工作区先检查远端，不重复添加或盲目改名：

```bash
# 直接从 GitHub 克隆：origin 自动指向研发主仓库。
git clone https://github.com/boundfree/boundfree-community.git boundfree-community
cd boundfree-community
git remote add gitee https://gitee.com/BigYellow12138/boundfree-community.git
```

```bash
# 从国内镜像克隆：取得 GitHub 权限后补齐研发远端。
git clone https://gitee.com/BigYellow12138/boundfree-community.git boundfree-community
cd boundfree-community
git remote rename origin gitee
git remote add origin https://github.com/boundfree/boundfree-community.git
git fetch origin master
git branch --set-upstream-to=origin/master master
```

## 1. 开始修改

先读根目录 [AGENTS.md](../AGENTS.md)，确认当前目录是 `boundfree-community`。工作空间根目录不是 Git 仓库；Pro 与授权服务不在本流程的改动范围内。

### 1.1 明确任务和权限

新增功能在 GitHub **Issues → New issue → 功能需求** 建立记录，说明使用场景、包含/不包含的范围、可检查的验收标准和兼容性影响。已有 Issue 就继续使用，不重复建单。作者认领任务，确定评审者或单人自审方式；Issue 分配给他人或发送邀请仍需相应任务授权。小型文档/维护变更可在 PR 正文承载这些信息。

本地修改、提交/提 PR、合入/镜像同步和对外发版按当前任务已授权的范围执行；已有授权无需重复确认。只要求本地修改时，完成验证并交付本地 diff；只要求提交或 PR 时，完成对应阶段并报告评审/合入状态。以下命令展示完整流程，不因阅读 SOP 自动获得所有步骤的授权。

### 1.2 从主线建分支

```bash
git status --short --branch
git remote -v
git fetch origin --prune
```

先识别已有改动和当前分支所属任务；未提交修改属于当前任务时继续保留，属于其他任务时先妥善保存或使用独立工作目录，不自动 stash、reset 或删除。远端名与第 0 节不一致时，先核对 URL 再替换命令，不盲目更改配置。

在干净工作目录中，从最新 GitHub 主线创建短期分支。下面名称为示例，Issue 编号和主题须替换为实际值：

```bash
git switch -c feature/123-channel-groups origin/master
git log -1 --oneline
```

分支类型按 [分支规范](development-guide.md#branch-model) 选择。已存在的同任务分支先检查关联 PR 和提交，再 `git switch <分支名>` 继续；已合入/关闭的分支不复用。不要求先更新本地 `master`，直接以 `origin/master` 为基线即可。

继续已共享分支时比较本地 head 与 `origin/<分支名>`；仅落后时用 `git merge --ff-only origin/<分支名>` 更新。两边分叉时先检查并整合远端改动，重新验证，不通过强推覆盖协作者提交。

### 1.3 处理前置 PR

当前分支存在未合入 PR 时，独立任务仍从 `origin/master` 分出。后续功能确实依赖前置 PR 时，优先等待前置任务合入；需要并行实现则使用堆叠 PR：

1. 核对前置 PR 的状态、head 分支与 SHA，从其最新远端 head 创建后续分支；在后续 PR 正文注明“依赖 #编号”、实际基线和合入顺序。
2. 后续 PR 暂以**前置分支**为 base，展示本任务自己的 diff；CI 当前只自动覆盖目标为 `master` 的 PR，因此此时不能声称已完成正式 PR CI。需要提前构建时使用已授权的手动 CI，并注明事件和 SHA。
3. 前置 PR 合入后，在本地将最新 `origin/master` merge 到后续分支；把后续 PR 的 base 改为 `master`（网页 **Edit** 或 `gh pr edit <编号> --base master`）之后再 push 该更新，触发正式 PR CI。重新检查 diff 与评审。当前工作流未监听仅修改 base 的 `edited` 事件，不能只改 base 就假定会构建；若没有新的分支更新、也没有符合条件的 PR 运行，可在已授权的 PR 操作范围内关闭再重新打开同一个 PR，触发 `reopened` 事件。确认两平台实际运行，不依赖旧绿色状态。

前置 PR 关闭且未合入时，后续任务保持阻塞；先重新确认依赖方案，不能把前置任务整体夹带合入。如果存在这样的依赖，也不要提前删除前置分支。

事件类型依据当前 `.github/workflows/ci.yml` 与 [GitHub 的 PR 事件说明](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request) 核对；工作流调整后同步维护此步骤。

## 2. 实现、文档与本地验证

实现保持在本次任务范围内，接口与用户可见行为变化同步更新相关契约、使用说明和有意义的回归。普通提交不修改产品版本；需要切换 dev / beta.N / stable 时，按发布 SOP 使用版本同步工具。

新增实现先按 [许可与扩展边界](../legal/LICENSING.md) 确定归属：Apache-2.0 核心和公开接口放本仓库，专有付费扩展放独立私有仓库。新增依赖/资源保留原始许可；涉及许可或打包声明时运行 `python3 scripts/common/check_release_assets.py`，核对 `LICENSE` / `NOTICE` 与随包副本一致。

提交前必做：

```bash
python3 scripts/common/version_sync.py check
git diff --check HEAD
git diff --stat
git diff
```

按改动类型增加验证，命令从仓库根目录执行：

| 改动 | 验证 |
| --- | --- |
| 仅文档、skill、PR 模板 | 检查链接、命令与实际工作流一致；新 skill 按下文自检 |
| C++ / Qt | 按 [工具链说明](toolchain-wsl2.md) 执行 CMake configure/build，运行受影响的原生回归 |
| JSON | `python3 -m json.tool <本次修改的文件>` |
| Issue 模板 / skill 元数据 YAML | 检查 YAML 解析、必填字段与对应平台格式 |
| Python 边车 | `python3 -m pytest tests/sidecar`，以及受影响的格式/IPC 契约回归 |
| UI 文案 / 翻译 | `python3 scripts/common/check_i18n.py`，以及对应 UI 验证 |
| 版本 / 发布脚本 / Actions | `python3 scripts/common/check_release_assets.py`、`python3 -m pytest tests/scripts/test_ci_version.py tests/scripts/test_release_preflight.py tests/scripts/test_gitee_release.py tests/scripts/test_m8_release_assets.py tests/scripts/test_windows_build_entry.py`；工作流 YAML 改动另跑 actionlint，并检查真实云端运行 |

本地环境无法运行的检查，应在 PR 写明，并检查云端相应结果。不要把“待 CI 验证”写成“通过”；也不要为仅文档改动编造功能验收。CI 会独立执行 Windows/Linux 完整构建与各自回归。

## 3. 暂存、提交与 PR

只暂存本次修改的明确路径，检查内容，避免把安装包、环境文件或私钥带入提交：

```bash
git add <本次修改的文件路径>
git diff --cached --check
git diff --cached --stat
git diff --cached
git commit -m "feat: 增加具体功能"
git push -u origin HEAD
```

提交的 type 按实际内容选择，允许类型与中文格式见开发规范；文档用 `docs`，构建流程用 `chore`。分支 push 本身不触发当前自动 CI；创建/更新目标为 `master` 的 PR 才触发 PR CI。

从 `.github/pull_request_template.md` 填写独立正文文件，写明 Issue、base/head、前置依赖、验收项、版本判断、已执行验证和未验收项。使用 `Closes #编号` 只关联本 PR 合入后确实完成的 Issue；部分实现用 `Refs #编号`，不能提前关闭整个需求。PR 标题也用中文 Conventional Commits。

实现尚未完成时可先创建 Draft；已就绪可去掉下面的 `--draft`：

```bash
gh pr create --base master --head feature/123-channel-groups \
  --title "feat: 增加通道分组" --body-file <已填写的PR正文文件> --draft
```

前置依赖按第 1.3 节设置实际 base。不要提交空模板或把换行写成字面量 `\n`。完成自查和验收项后，用网页 **Ready for review** 或 `gh pr ready <PR编号>` 转为待评审。多人协作通过右侧 **Reviewers** 指定已约定且有权限的评审者；单人维护按开发规范记录自审。已存在同任务 PR 时继续更新，不为每个提交另开 PR。

## 4. 检查 PR、合入与主线包

### 4.1 评审和返修

作者先自查 PR 的 **Files changed** 与验收项。评审者检查范围是否符合 Issue、实现和数据处理是否正确、契约与许可是否一致，以及验证能否证明承诺行为；仅文档变更检查规则、链接与命令。留下 **Approve**、**Request changes** 或普通评论，说明具体问题和结论。存在阻塞意见时先处理；“已回复”不代表已解决。

作者在原分支修改、执行受影响验证、提交并 push，在对应讨论说明处理结果，再请求复审。更新 PR 描述中的最终范围与验证证据，不保留失效说明。单人维护的自审记录至少包含最新 head SHA、检查过的 diff/验收项、验证结果和是否可合入；AI 辅助结果注明实际执行者，不伪造他人批准。

PR 和 `master` 更新自动运行 `Community CI`，执行双平台编译、回归、打包与安装态 smoke。检查最新运行是否对应本次提交；两平台都必须实际成功，不能把缺失、跳过、取消或仅 Linux 成功视为通过。失败先看具体 step 与 `reports-*`；源码问题修复后提交，临时网络问题可 Re-run all jobs，保持两平台尝试编号一致。PR 不获得签名 secrets。

### 4.2 主线更新与冲突

合入前 fetch 并检查最新主线是否已包含在当前任务分支：

```bash
git fetch origin master
git merge-base --is-ancestor origin/master HEAD
```

此命令须在本 PR 的 head 分支运行。返回 0 表示包含最新主线；返回 1 则在干净工作目录中执行 `git merge origin/master`。有冲突时逐文件按两边意图解决，使用明确路径 `git add <已解决的文件>` 并完成 merge commit；不能整批使用 ours/theirs 吞掉另一边。必要时 `git merge --abort` 退出尚未完成的合并，再重新确认方案。其他错误先排查，不直接进入下一步。

主线更新完成后执行相关验证、push 原分支，重新检查受影响 diff、评审和最新 CI。不 rebase 已共享分支、不强推。合入前再次核对当前主线，防止等待期间其他 PR 已改变基线。

### 4.3 合入前核对与执行

维护者按当前任务授权合入；以下条件都满足后执行：

- PR 已 Ready，目标为 `master`，前置 PR 已合入，验收范围和 PR diff 一致。
- 最新 head 已完成所需评审，阻塞意见已解决；单人维护有对应 SHA 的自审记录。
- 最新 head 包含当前 `origin/master`，对应 PR 的 Windows/Linux CI 都成功且无冲突。
- 版本判断、必要验证与相关文档已完成；平台规则若生效还须满足其额外要求。

先查看并记录实际 head SHA、base 和评审/检查结果：

```bash
gh pr view <PR编号> --json state,isDraft,baseRefName,headRefName,headRefOid,mergeable,reviewDecision,statusCheckRollup
gh pr diff <PR编号>
gh pr checks <PR编号>
```

`reviewDecision` 为空可能只是没有配置平台必需评审，不代表批准；`mergeable` 或绿色按钮也不能代替上面的人工核对。平台保护能力与当前限制见 [开发规范](development-guide.md#平台保护)。

网页选择 **Create a merge commit**，或用已核对的完整 head SHA 执行：

```bash
gh pr merge <PR编号> --merge --match-head-commit <已核对的head完整SHA> \
  --subject "feat: 集成通道分组（#<PR编号>）"
```

按实际任务选择提交 type 和标题。`--match-head-commit` 可避免误合入核对后新增的提交，但不会代替 CI/评审或锁定 base；失败后重新读取状态，不用 `--admin` 绕过。API/网络结果不明确时先查 PR 状态，避免重复操作。通过网页合入也要在点击前核对最新 head 和主线。本步骤先保留分支，到第 5 节确认依赖后再清理。

### 4.4 合入后核对

```bash
gh pr view <PR编号> --json state,mergedAt,mergeCommit,url
git fetch origin master
```

确认 PR 为 `MERGED`，记录 `mergeCommit.oid`；再核对本次 **push / master** 的 `Community CI` 运行，`headSha` 应为该合并提交，等待两个平台完成。若之后又有主线更新，分别记录本 PR 合并提交和最终要交付的主线 SHA，检查相应运行，不混淆两者。PR 测试合并提交可能与最终主线 SHA 不同，不能用 PR 产物替代主线产物。

```bash
gh run list --workflow ci.yml --branch master --event push --commit <主线完整SHA>
gh run view <运行ID> --json headSha,event,status,conclusion,url,jobs
```

按 [安装包下载与核验](release-sop.md#2-安装包下载与核验) 检查该成功运行的内部 dev 包。普通 CI 不创建 tag 或 Release，产品版本不会被回写。主线 CI 失败或运行被后续 push 取消时，先排查并验证最终交付主线；在成功前不报告交付完成、不同步 Gitee。必要修复走新 PR；需要撤销时在新分支使用 revert 并提 PR，不 reset 或强推主线。

手动重建使用 **Actions → Community CI → Run workflow** 选择分支；该入口要求工作流已合入默认分支。

## 5. 同步 Gitee 与收尾

完成第 4 节主线 CI 和包核验后再同步。先记录本次已核验的主线完整 SHA，fetch 后确认 `origin/master` 仍是该提交；如果主线已变化，先核验新的主线再同步，不能只验证旧提交却推送新提交。然后检查 Gitee 当前主线：

```bash
git fetch origin master
git fetch gitee master
git log --oneline --left-right origin/master...gitee/master
git merge-base --is-ancestor gitee/master origin/master
```

只有最后一条返回 0，且确认主线就是要同步的代码时，才快进推送。用已核验的完整 SHA 固定本次同步目标，避免读取可能变化的本地远端跟踪引用：

```bash
git push gitee <已核验的主线完整SHA>:refs/heads/master
git ls-remote origin refs/heads/master
git ls-remote gitee refs/heads/master
```

若历史分叉，停止推送，把差异带回 GitHub 审查整合；不强推、不使用 `git push --mirror`，不覆盖 Gitee 独有提交。

比较两端远端 SHA 与本次交付目标；如果 GitHub 又前进，区分本次已同步提交与后续待验提交，不误报全部最新内容已同步。不为记录 CI 结果另造源码提交；平台简介与链接需单独核对，不能由 Git 推送代替。

分支清理是最后一步，限于本次任务的已合入分支。确认 PR 为 `MERGED`、工作目录干净、没有其他 PR 以该分支为 base，且没有其他 worktree/协作者仍使用它：

```bash
git fetch origin --prune
git worktree list
git switch master
git merge --ff-only origin/master
git merge-base --is-ancestor <本次任务分支> master
```

仅当本地 `master` 能快进、最后一条返回 0 且上述依赖检查完成，才继续。远端分支仍存在时，另运行 `git merge-base --is-ancestor origin/<本次任务分支> master`，必须返回 0；远端出现未合入的新提交时保留分支，不能只检查本地旧 head 就删除。确认后执行：

```bash
git push origin --delete <本次任务分支>
git branch -d <本次任务分支>
git fetch origin --prune
```

远端已被 GitHub 删除时跳过第一条；`git branch -d` 拒绝或本地主线分叉时先查原因，不改用 `-D` 或 reset。关闭但未合入的 PR 先记录取消原因并保留有用工作；删除其分支需确认放弃，不按已合入流程自动清理。Gitee 不同步临时功能分支，也不批量删除它的历史分支。

结束时记录 Issue/PR、分支、合并与交付 SHA、评审方式、CI/包核验、两端同步和分支清理状态、产品版本及仍待验收事项。只完成本地修改时报告当前分支和 diff，PR/合入/CI 等未执行阶段如实标明。

## 6. 发版交接

完整开发交付以主线验证、源码同步和本次分支收尾为完成点，Release 状态记录为“不适用”；只授权较早阶段的任务按实际完成范围报告。明确要交付 beta/stable 时，按 [版本与发布 SOP](release-sop.md) 操作，并单独记录 Release 同步状态。本文不授权自动合并或公开发布。

## 7. 仓库 skill 与维护约定

仓库技能 [community-delivery](../.agents/skills/community-delivery/SKILL.md) 随 Git 分发，可显式调用 `$community-delivery`；它只路由到 SOP，不复制流程。不支持仓库技能的工具直接读取 `AGENTS.md` 和本文。

流程调整时核对 README、AGENTS、规范、两份 SOP、skill 和 PR/Issue 模板，只更新受影响的入口与交接链接。仓库简介属于平台设置：GitHub 标明研发主仓库，Gitee 标明国内源码/Release 镜像及 GitHub 协作入口；更新文字不改变可见性或协作者。

Skill 文件使用标准 `name` / `description` YAML front matter；不套用 `docs/` 的五字段文档模板。更新技能后检查相对链接可解析、入口描述能区分 Community 与 Pro、无脚手架占位符；若本机安装了 skill-creator，运行其 `scripts/quick_validate.py .agents/skills/community-delivery`。该工具不属于产品构建依赖，没有安装时不阻断应用编译，但应手动完成上述自检。
