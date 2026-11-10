---
title: 提交与交付 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-11-10
---

# 提交与交付 SOP

从一个任务到它进入主线的完整步骤。第 1 节是日常路径，照着做即可；第 2 节解释每一步的要求；第 3 节是不常遇到的特殊情况。

分支命名、提交格式和评审规则在 [开发规范](development-guide.md)，版本与发版在 [发布 SOP](release-sop.md)。本流程只适用于 Community，不适用于 Pro 和授权服务端。

## 1. 日常路径

```bash
# ① 从最新主线建分支（名称按任务替换）
git fetch origin --prune
git switch -c feature/123-channel-groups origin/master

# ② 修改代码，按改动类型验证（见 2.2），提交前必跑这两条
python3 scripts/common/version_sync.py check
git diff --check HEAD

# ③ 只暂存本次改动，检查后提交并推送
git add <本次修改的文件>
git diff --cached
git commit -m "feat: 增加通道分组"
git push -u origin HEAD

# ④ 开 PR（正文按 .github/pull_request_template.md 填写）
gh pr create --base master --title "feat: 增加通道分组" --body-file <PR正文文件>

# ⑤ 等双平台 CI 通过，自查 diff，记录自审结论，然后合入
gh pr checks <PR编号>
gh pr view <PR编号> --json headRefOid,mergeStateStatus
gh pr merge <PR编号> --merge --match-head-commit <已核对的完整SHA> \
  --subject "feat: 集成通道分组（#<PR编号>）"

# ⑥ 等主线 CI 通过，把已验证的提交同步到 Gitee
gh run list --workflow ci.yml --branch master --event push --limit 1
git fetch origin master && git fetch gitee master
git merge-base --is-ancestor gitee/master origin/master   # 返回 0 才能推
git push gitee <已验证的主线完整SHA>:refs/heads/master

# ⑦ 清理本次分支
git switch master && git merge --ff-only origin/master
git push origin --delete feature/123-channel-groups
git branch -d feature/123-channel-groups
```

七步对应：建分支 → 验证 → 提交 → PR → 合入 → 同步镜像 → 清理。任务只授权到其中某一步时，做到那一步为止并如实报告，不因为读了本文就继续往下做。

## 2. 每一步的要求

### 2.1 建分支

- 先看 `git status --short --branch`。已有未提交改动属于别的任务时，不要 stash、reset 或删除，改用独立工作目录。
- 一个可独立验收的任务对应一个分支和一个 PR。已合入或已关闭的分支不复用。
- 新增功能先在 GitHub 建 Issue（功能需求模板），写清范围和验收标准。小型文档或维护改动可以直接在 PR 正文说明。
- 本机在 Linux/WSL 源码目录操作。Windows 编译副本只接收单向同步，见 [工具链说明](toolchain-wsl2.md)。

### 2.2 验证

| 改动 | 验证 |
| --- | --- |
| 任何改动 | `python3 scripts/common/version_sync.py check`、`git diff --check HEAD` |
| C++ / Qt | 按 [工具链说明](toolchain-wsl2.md) 执行 CMake configure 与 build，运行受影响的原生回归 |
| Python 边车 | `python3 -m pytest tests/sidecar`，以及受影响的格式或 IPC 契约回归 |
| JSON | `python3 -m json.tool <文件>` |
| 界面文字或翻译 | `python3 scripts/common/check_i18n.py` |
| 许可、打包声明、新依赖 | `python3 scripts/common/check_release_assets.py`，并按 [许可与扩展边界](../legal/LICENSING.md) 确认归属 |
| 版本、发布脚本、Actions | `python3 scripts/common/check_release_assets.py`、`python3 -m pytest tests/scripts`；工作流 YAML 另跑 actionlint |
| 仅文档 | 检查链接和命令与实际一致 |

本地跑不了的检查写进 PR，并以云端结果为准。没执行的检查不能写成“通过”。普通提交不改产品版本。

### 2.3 提交与 PR

- 提交信息用中文 Conventional Commits，格式见 [开发规范](development-guide.md#commit-rules)。PR 标题同样格式。
- 暂存时写明确路径，不用 `git add -A` 把安装包、环境文件或私钥带进去。
- PR 正文写清关联 Issue、验收项、版本判断、已执行的验证和未验收项。合入后确实完成的 Issue 用 `Closes #编号`，部分实现用 `Refs #编号`。
- 实现未完成时加 `--draft`，完成后 `gh pr ready <PR编号>`。同一任务持续更新同一个 PR。
- 只推送分支不会触发 CI，目标为 `master` 的 PR 才会。

### 2.4 评审与合入

合入前四项都要满足：

1. PR 不是 Draft，目标是 `master`，diff 与验收范围一致。
2. 最新提交上 `build / package (ubuntu-24.04)` 和 `build / package (windows-2022)` 都成功。缺失、跳过、取消或只有一个平台成功都不算通过。
3. 分支已包含最新 `origin/master`（分支保护会强制这一点）。
4. 评审完成。单人维护时在 PR 里记录自审：最新 head SHA、检查过的 diff 与验收项、验证结果、是否可合入。AI 辅助的结果注明实际执行者，不伪造他人批准。

合入方式固定为 **Create a merge commit**，用 `--match-head-commit` 锁定核对过的提交。合入失败时重新读取状态，不用 `--admin` 绕过。

CI 失败先看具体步骤和 `reports-*` 产物。源码问题修复后提交；临时网络问题可以 Re-run all jobs。

### 2.5 合入后

- 确认 PR 状态为 `MERGED`，记录合并提交的 SHA。
- 等 `push / master` 的 CI 在该 SHA 上双平台成功。PR 的 CI 产物不能代替主线产物。
- 需要内部安装包时，按 [发布 SOP 第 2 节](release-sop.md#2-安装包下载与核验) 核对 `build-info` 和 SHA256。
- 主线 CI 失败时不同步 Gitee，走新 PR 修复或 revert，不 reset、不强推主线。

### 2.6 同步 Gitee

GitHub 是研发主仓库，Gitee 是国内镜像，方向只有 GitHub → Gitee。

- 只在主线 CI 成功后同步，并且用完整 SHA 指定目标。
- `git merge-base --is-ancestor gitee/master origin/master` 返回 0 才能推。历史分叉时停止，把差异带回 GitHub 处理，不强推、不用 `--mirror`。
- 同步后用 `git ls-remote origin refs/heads/master` 和 `git ls-remote gitee refs/heads/master` 核对两端一致。
- 这里只同步源码。tag 和 Release 的同步归 [发布 SOP](release-sop.md)。

### 2.7 清理

只清理本次已合入的分支。先确认没有其他 PR 以它为 base，也没有其他工作目录在用它（`git worktree list`）。`git branch -d` 拒绝时先查原因，不改用 `-D`。不批量删除历史分支。

### 2.8 结束时报告

说明实际改动、产品版本是否变化、验证结果、分支与 PR、评审方式、主线 CI、Gitee 同步和分支清理状态，以及仍待验收的事项。没执行的阶段如实标明。

## 3. 特殊情况

### 3.1 主线在 PR 期间前进了

```bash
git fetch origin master
git merge origin/master        # 在 PR 分支上执行
git push
```

有冲突时逐文件按两边意图解决，不整批用 ours 或 theirs。不 rebase 已推送的分支，不强推。推送后等新一轮 CI，旧的绿色结果不算数。

### 3.2 任务依赖另一个未合入的 PR

优先等前置 PR 合入。必须并行时使用堆叠 PR：

1. 从前置 PR 的最新远端分支建分支，后续 PR 的 base 设为前置分支，正文注明“依赖 #编号”。
2. 此时 CI 不会自动运行（工作流只监听目标为 `master` 的 PR），不能声称 CI 通过。
3. 前置 PR 合入后，把 `origin/master` merge 进后续分支，用 `gh pr edit <编号> --base master` 改 base，再推送以触发 CI。只改 base 不推送不会触发构建；没有新提交可推时，关闭再重新打开 PR。

前置 PR 被关闭且未合入时，后续任务保持阻塞，不把前置改动夹带合入。

### 3.3 合入后发现问题

在新的 `fix/*` 分支修复，或在新分支上 `git revert` 后提 PR。已发布版本的紧急缺陷用 `hotfix/*`，同样从最新 `origin/master` 建分支并走完整流程。

### 3.4 PR 关闭但未合入

记录取消原因，保留有用的工作。删除分支需要确认放弃，不按已合入流程自动清理。

### 3.5 新成员与权限

- 组织管理员在 [People](https://github.com/orgs/boundfree/people) 以 Member 身份邀请。组织默认仓库权限是 Read，参与开发需要在本仓库 **Settings → Collaborators and teams** 另授 Write。
- 仓库公开，任何人都可以提 Issue 或从 fork 发起 PR。外部 fork 的 PR 首次运行 Actions 需要维护者批准，且拿不到仓库机密。
- Issue、PR、评审统一在 GitHub，不在 Gitee 另建。

新克隆后补齐两个远端：

```bash
git clone https://github.com/boundfree/boundfree-community.git
cd boundfree-community
git remote add gitee https://gitee.com/BigYellow12138/boundfree-community.git
```

从 Gitee 克隆的，把 `origin` 改名为 `gitee`，再添加 GitHub 为 `origin` 并让 `master` 跟踪 `origin/master`。

### 3.6 发版

本流程以主线验证、镜像同步和分支清理为终点。要交付 beta 或 stable 时，转到 [发布 SOP](release-sop.md)。本文不授权创建 tag 或公开发布。

## 4. 维护本流程

流程调整时，在同一个 PR 里核对 `AGENTS.md`、[开发规范](development-guide.md)、[发布 SOP](release-sop.md)、仓库技能 [community-delivery](../.agents/skills/community-delivery/SKILL.md) 和 PR/Issue 模板，只更新受影响的部分。技能只负责路由到本文，不复制步骤。
