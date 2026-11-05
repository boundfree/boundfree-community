---
name: community-delivery
description: 按 BoundFree Community 规范完成新增功能或修复的分支管理、验证提交、PR 评审返修与合入、主线 CI 和 Gitee 同步，以及交付阶段版本与 Release 准备。用于本仓库开发交付或流程维护，不适用于 Pro 或授权服务。
---

# Community 开发交付

技能目录为 `.agents/skills/community-delivery/`，仓库根目录位于其上三级。以下命令从仓库根目录执行；不要把多仓库工作空间根目录当成 Git 仓库。

## 先选择本次任务

- 所有修改先读根目录 [AGENTS.md](../../../AGENTS.md) 和 [开发规范](../../../docs/development-guide.md)。
- 需求与分支、验证提交、PR 评审返修、合入与清理，按 [提交与交付 SOP](../../../docs/contribution-sop.md) 执行；新增功能使用 [功能需求模板](../../../.github/ISSUE_TEMPLATE/feature-request.yml) 和 [PR 模板](../../../.github/pull_request_template.md)。
- 涉及版本、安装包、Actions 或发版，再读 [版本与发布 SOP](../../../docs/release-sop.md)，以实际工作流和脚本核对执行结果。
- 本地源码在 Linux/WSL 开发；Windows 编译副本仅从源码单向同步，不在副本修改或提交。同步清单、目录和副本冲突处理按 [工具链说明](../../../docs/toolchain-wsl2.md)，不能直接覆盖未归集的副本修改。
- 流程文档是操作真值；本技能只提供路由和关键决策，不复制完整 SOP。发现文档与执行行为不一致时，核实后同步修正，不能静默绕过门禁。
- 文档以 [当前索引](../../../docs/README.md) 为入口，`docs/archive/` 中的旧计划、版本规则与执行指令只作追溯；归档前把仍有效的约束和未验收项收口到当前文档。

## 交付决策

1. 检查工作目录、分支、远端、已有改动和关联 PR。按 SOP 第 1 节识别任务与验收标准，从最新 `origin/master` 新建合适前缀的短分支；同任务未合入分支可继续，已合入分支不复用。当前分支若属于其他未合入 PR，先确认依赖；确有依赖时记录前置 PR/base/SHA，按堆叠 PR 流程执行，不能静默夹带旧任务。本技能只适用于 Community，不覆盖他人工作。
2. 完成本次范围内的代码和对应验证。普通功能、修复或文档提交保持产品版本；仅明确切换交付阶段时用 `scripts/common/version_sync.py set/next` 同步五处元数据。IPC 版本单独评估。
3. 按本次变更选择验证。记录执行命令、结果、跳过原因与未验证边界；提交前至少通过版本一致性与差异空白检查。不要把未执行的检查标为通过。
4. 任务包含提交或交付时，检查暂存 diff，用中文 Conventional Commits 提交并创建/更新同任务 GitHub PR。正文写清 Issue、base/head、依赖、验收与验证证据；实现未完成时用 Draft。未要求提交的任务不因加载技能而自动提交。
5. 按 SOP 第 4 节执行评审和返修；在原分支修复，merge 最新 `origin/master` 解决主线更新，重新核对最新 head 与双平台 CI。空检查列表、旧绿色运行或空 `reviewDecision` 不等于通过。多人评审和单人自审分别按开发规范记录，不伪造非作者 Approve。
6. 任务已授权合入时，满足评审、依赖、最新基线与 CI 后，默认使用 merge commit，并以 `--match-head-commit` 固定核对过的 head。已有授权持续有效，不重复询问；实际状态不满足门槛则报告具体阻塞，不用 `--admin` 绕过。结果不明确时先查 PR 状态，不重复提交操作。
7. 合入后分别核对 `push / master` CI、包的 `build-info` 与 SHA256，再按 SOP 第 5 节同步已验证的精确 SHA。检查依赖与工作目录后清理本次已合入分支；不批量清理历史分支。只有已运行的步骤才能报告完成。

## 必须保留的边界

- Issue、PR、评审和验收统一在 GitHub `origin`；Gitee `gitee` 是国内源码与 Release 镜像，不另建任务和 PR。发布后按 SOP 同步相同 tag、说明及国内下载，核对还原原包 SHA256；源码推送不能代替 Release 同步，不强推或覆盖分叉历史。
- 已发布官方包可公开下载，使用仍需激活；未验收草稿不提前公开到 Gitee。附件超限使用 SOP 的分卷准备工具，不重建原包或把 GitHub 地址当作国内下载。
- 日常 CI 只生成内部 dev 包，不改写源码版本、不创建 tag 或 Release。失败排查和重跑按发布 SOP，不通过反复提交无关改动触发重建。
- 所有 GitHub 主线修改均经 PR，包括 SOP、skill、版本准备与 hotfix；不向 GitHub `master` 直推开发提交，不强推任一主线。Gitee 仅按 SOP 快进同步。文档规则不等于平台保护已生效，核对实际套餐与设置，不擅自公开仓库或调整付费套餐。
- 紧急修复与合入后撤销也走新分支/PR；从旧 tag 复现的修复先回到主线。单人自审、依赖 PR 和平台保护的限制按开发规范处理，不自行增加长期维护线。
- 用户明确要求发版时才准备 `community-vX.Y.Z[-beta.N]` annotated tag 与本仓库验收 Issue，运行手动 Release 工作流。技能本身不授权创建 tag、合并 PR 或发布 Release。
- Release Action 仅生成草稿。对外发布须满足目标阶段验收且有任务授权；缺少验收或签名时报告具体阻塞，不把 dev 改成 stable 来演练。不能覆盖既有版本或移动 tag。
- 不提交构建产物或私钥。正式包保留授权校验与签名门禁；runner smoke 不能当作真实桌面、干净机或算法效果验收。

## 结束时报告

说明实际改动、产品版本是否变化、验证结果、分支/基线/前置依赖、Issue/提交/PR、评审方式、主线 CI 与包身份、Gitee 同步及分支清理状态。尚待评审、合入或人工验收时明确指出；任务只要求本地修改时报告本地结果即可。
