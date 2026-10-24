---
title: Community 提交与交付 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# Community 提交与交付 SOP

适用于本仓库的人工开发者与代码代理，从开始修改到合入后检查安装包。日常规则以 [开发规范](development-guide.md) 为准；版本操作、发版门槛、签名和失败恢复以 [版本与发布 SOP](release-sop.md) 为准。本文不授权自动合并、创建 tag 或对外发布；执行这些动作应符合任务授权和维护权限。

Issue、PR、代码评审和发版验收统一在 [GitHub](https://github.com/Vsonic12138/boundfree-eeg-native) 管理；Gitee 只同步源码和 Release，不在两个平台维护两套任务或评审记录。GitHub 当前访问权限不因本流程改变。

## 1. 开始修改

先读根目录 [AGENTS.md](../AGENTS.md)，确认当前目录是 `boundfree-community`。工作空间根目录不是 Git 仓库；Pro 与授权服务不在本流程的改动范围内。

```bash
git status --short --branch
git remote -v
git fetch origin master
```

先处理或保留已有改动，不为更新分支而 reset、覆盖或删除他人工作。在干净工作目录中，从 GitHub 主线创建短期分支，例如：

```bash
git switch -c feature/your-change origin/master
```

缺陷、文档和技术验证分别使用 `fix/*`、`docs/*`、`spike/*`；不要把示例分支名原样作为长期开发分支。`origin` 应为 GitHub 的本仓库 `Vsonic12138/boundfree-eeg-native`，`gitee` 应为 Gitee 的 `BigYellow12138/boundfree-community`；若远端名不同，先核对实际 URL 后替换后续命令，不盲目更改配置。

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
| C++ / Qt | 按开发规范执行 CMake configure/build，运行受影响的原生回归 |
| Python 边车 | `python3 -m pytest tests/sidecar`，以及受影响的格式/IPC 契约回归 |
| UI 文案 / 翻译 | `python3 scripts/common/check_i18n.py`，以及对应 UI 验证 |
| 版本 / 发布脚本 / Actions | `python3 scripts/common/check_release_assets.py`、`python3 -m pytest tests/scripts/test_ci_version.py tests/scripts/test_release_preflight.py tests/scripts/test_gitee_release.py tests/scripts/test_m8_release_assets.py tests/scripts/test_windows_build_entry.py`；YAML 改动另跑 actionlint，并检查真实云端运行 |

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

提交的 type 按实际内容选择，允许类型与中文格式见开发规范；文档用 `docs`，构建流程用 `chore`。使用仓库 `.github/pull_request_template.md` 向 GitHub `master` 创建 PR，说明问题和变化、版本判断、已执行验证、未验收项。命令行可用 `gh pr create --base master --body-file <已填写的PR正文文件>`；不要提交空模板或把正文换行写成字面量 `\n`。

## 4. 检查 PR、合入与主线包

1. 检查 PR diff 和 `Community CI` 的最新一次运行，确保它对应最新改动。失败先看具体 step 与 `reports-*`，按发布 SOP 修复或重跑。
2. Windows 和 Linux job 都成功、审查要求满足，且任务授权合入后，由维护者合入 `master`。分支保护/Ruleset 由仓库设置单独管理；本文、skill 和 PR 模板本身不能阻止 Git 直接推送，未配置保护时不能声称已强制拦截。
3. 合入会再次触发 `Community CI`。查看此次 **push / master** 运行，核对 `headSha` 为合入后的主线 SHA，等待两个平台完成。PR 使用的测试合并提交可能与最终主线 SHA 不同，不能混用。
4. 从该运行下载 `BoundFreeEEG-Windows-x64` / `BoundFreeEEG-Linux-x64`，按发布 SOP 核对包内版本、`build-info-*.json`、源提交与 `SHA256SUMS-*.txt`。日常交付停在内部 dev 构建，不需要 tag 或 Release。

可使用以下命令辅助核对，`<主线完整SHA>` 替换为本次实际提交；查看 job 结论时仍应确认运行的来源事件：

```bash
gh pr checks <PR编号>
gh run list --workflow ci.yml --branch master --event push --commit <主线完整SHA>
gh run view <运行ID> --json headSha,event,status,conclusion,url,jobs
```

## 5. 同步 Gitee 与收尾

源码同步前，先确认 GitHub 合入结果和 Gitee 当前主线：

```bash
git fetch origin master
git fetch gitee master
git log --oneline --left-right origin/master...gitee/master
git merge-base --is-ancestor gitee/master origin/master
```

只有最后一条返回 0，且确认主线就是要同步的代码时，才快进推送：

```bash
git push gitee refs/remotes/origin/master:refs/heads/master
git ls-remote origin refs/heads/master
git ls-remote gitee refs/heads/master
```

若历史分叉，保留双方提交并评审整合，不强推；无关任务不顺手同步。完成后记录本次提交/PR、主线 SHA、CI 链接、实际验证及限制，说明是否已同步。无需为了收尾再修改产品版本，也无需把每次 CI 结果写成新的源码提交。

涉及发布的任务还必须完成发布 SOP 第 7 节的 Gitee Release 同步与下载验证，并把两边 URL、源 SHA 和校验结果记入同一个 GitHub 验收 Issue。源码同步成功不等于 Release 同步成功；日常未发版的提交不创建 Gitee Release。

## 6. 需要试用或正式发布时

转到 [版本与发布 SOP](release-sop.md) 第 3–7 节，使用本仓库 **Community 发版验收** Issue 模板记录证据。合入版本准备 PR、等待主线 CI、创建 annotated tag、手动执行 `Community Release`、检查草稿，再按授权决定是否 Publish。普通功能任务不能自动扩展为正式发版；纯演练不能绕过验收或稳定版签名门禁。

## 7. 仓库 skill 与维护约定

仓库技能入口为 [.agents/skills/community-delivery/SKILL.md](../.agents/skills/community-delivery/SKILL.md)，随 Git 一起分发，不依赖开发者复制到个人目录。在支持仓库技能的代理中可用 `$community-delivery`；不支持时直接要求代理阅读该文件和根目录 `AGENTS.md`，人工开发者从本文开始。

技能只引用操作文档，不维护另一份流程。修改分支、版本、提交或 Actions 行为时，在同一 PR 检查开发规范、本文、发布 SOP、skill、`AGENTS.md`、README 与 PR/Issue 模板；只更新实际受影响的内容。保留文档与实现的可追溯关系，不把旧报告当作当前规范。

Skill 文件使用标准 `name` / `description` YAML front matter；不套用 `docs/` 的五字段文档模板。更新技能后检查相对链接可解析、入口描述能区分 Community 与 Pro、无脚手架占位符；若本机安装了 skill-creator，运行其 `scripts/quick_validate.py .agents/skills/community-delivery`。该工具不属于产品构建依赖，没有安装时不阻断应用编译，但应手动完成上述自检。
