---
title: Community 提交与交付 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# Community 提交与交付 SOP

本文负责日常开发交付：配置远端 → 修改与验证 → PR → 合入与主线包核对 → Gitee 源码同步。分支、提交格式和文档要求见 [开发规范](development-guide.md)；涉及产品版本、tag 或对外发版时，从第 6 节交接到发布 SOP。

## 0. 两个托管地址与协作顺序

GitHub 与 Gitee 托管的是**同一个 Community 项目**，共用 `master`、版本文件、提交历史与 `community-v*` tag。GitHub 是研发与发布记录的来源，Gitee 是国内镜像；不是各自维护一套功能的两个开发仓库。本流程不适用于另一个产品 `boundfree-pro`。

| 项目 | GitHub `origin` | Gitee `gitee` |
| --- | --- | --- |
| 仓库 | `Vsonic12138/boundfree-eeg-native` | `BigYellow12138/boundfree-community` |
| 当前可见性 | 私有，需仓库访问权限 | 公开 |
| Issue、PR、评审与验收 | 唯一协作入口 | 不新建、不复制，不做另一轮合并 |
| 开发与集成 | 短分支 → PR → `master` | 同步 GitHub 已验证的主线，避免直接开发 |
| 构建 | GitHub Actions 自动执行 | 不承担第二套构建 |
| 发布 | 验收记录、原始构建、草稿与首次发布 | 同 tag、源提交和说明，提供国内下载镜像 |
| 文档 | 随代码在 GitHub PR 中更新 | 随主线同步，不单独在线编辑 |

**同步方向：GitHub → Gitee。** 本文第 5 节同步源码；tag 和 Release 的同步归发布 SOP 管理。

新成员先取得 GitHub 协作权限，再在其 [Issues](https://github.com/Vsonic12138/boundfree-eeg-native/issues) / [Pull requests](https://github.com/Vsonic12138/boundfree-eeg-native/pulls) 工作。未获权限时页面可能显示 404，应联系仓库维护者申请访问，不在 Gitee 建立替代任务库。本流程不自动公开 GitHub 或邀请协作者。

新克隆可二选一；以下只用于新目录，已有工作区先检查远端，不重复添加或盲目改名：

```bash
# 直接从 GitHub 克隆：origin 自动指向研发主仓库。
git clone https://github.com/Vsonic12138/boundfree-eeg-native.git boundfree-community
cd boundfree-community
git remote add gitee https://gitee.com/BigYellow12138/boundfree-community.git
```

```bash
# 从国内镜像克隆：取得 GitHub 权限后补齐研发远端。
git clone https://gitee.com/BigYellow12138/boundfree-community.git boundfree-community
cd boundfree-community
git remote rename origin gitee
git remote add origin https://github.com/Vsonic12138/boundfree-eeg-native.git
git fetch origin master
git branch --set-upstream-to=origin/master master
```

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

分支类型按 [分支规范](development-guide.md#branch-model) 选择。远端名与第 0 节不一致时，先核对 URL 再替换命令中的名字，不盲目更改配置。

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

1. PR 和 `master` 更新自动运行 `Community CI`，执行双平台编译、回归、打包与安装态 smoke。检查 PR diff 和最新运行是否对应本次提交。失败先看具体 step 与 `reports-*`；源码问题修复后提交，临时网络问题可 Re-run all jobs，保持两平台尝试编号一致。PR 不获得签名 secrets。
2. Windows 和 Linux job 都成功、审查要求满足，且任务授权合入后，由维护者合入 `master`。分支保护/Ruleset 由仓库设置单独管理；本文、skill 和 PR 模板本身不能阻止 Git 直接推送，未配置保护时不能声称已强制拦截。
3. 合入会再次触发 `Community CI`。查看此次 **push / master** 运行，核对 `headSha` 为合入后的主线 SHA，等待两个平台完成。PR 使用的测试合并提交可能与最终主线 SHA 不同，不能混用。
4. 按 [安装包下载与核验](release-sop.md#2-安装包下载与核验) 检查该运行的内部 dev 包。普通 CI 不创建 tag 或 Release，产品版本不会被回写。

可使用以下命令辅助核对，`<主线完整SHA>` 替换为本次实际提交；查看 job 结论时仍应确认运行的来源事件：

```bash
gh pr checks <PR编号>
gh run list --workflow ci.yml --branch master --event push --commit <主线完整SHA>
gh run view <运行ID> --json headSha,event,status,conclusion,url,jobs
```

手动重建使用 **Actions → Community CI → Run workflow** 选择分支；该入口要求工作流已合入默认分支。

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

若历史分叉，停止推送，把差异带回 GitHub 审查整合；不强推、不使用 `git push --mirror`，不覆盖 Gitee 独有提交。

记录提交/PR、主线 SHA、CI 与包核验结果、源码同步状态和限制。不为记录 CI 结果另造源码提交；平台简介与链接需单独核对，不能由 Git 推送代替。

## 6. 发版交接

普通开发任务到源码同步即完成，Release 状态记录为“不适用”。明确要交付 beta/stable 时，按 [版本与发布 SOP](release-sop.md) 操作，并单独记录 Release 同步状态。本文不授权自动合并或公开发布。

## 7. 仓库 skill 与维护约定

仓库技能 [community-delivery](../.agents/skills/community-delivery/SKILL.md) 随 Git 分发，可显式调用 `$community-delivery`；它只路由到 SOP，不复制流程。不支持仓库技能的工具直接读取 `AGENTS.md` 和本文。

流程调整时核对 README、AGENTS、规范、两份 SOP、skill 和 PR/Issue 模板，只更新受影响的入口与交接链接。仓库简介属于平台设置：GitHub 标明研发主仓库，Gitee 标明国内源码/Release 镜像及 GitHub 协作入口；更新文字不改变可见性或协作者。

Skill 文件使用标准 `name` / `description` YAML front matter；不套用 `docs/` 的五字段文档模板。更新技能后检查相对链接可解析、入口描述能区分 Community 与 Pro、无脚手架占位符；若本机安装了 skill-creator，运行其 `scripts/quick_validate.py .agents/skills/community-delivery`。该工具不属于产品构建依赖，没有安装时不阻断应用编译，但应手动完成上述自检。
