---
name: community-delivery
description: 按 BoundFree Community 仓库规范完成开发修改的验证、Git 提交与 PR、交付阶段版本调整、GitHub Actions 构建排障和 Release 草稿准备。用于本仓库的开发交付任务，不适用于 Pro 或授权服务。
---

# Community 开发交付

技能目录为 `.agents/skills/community-delivery/`，仓库根目录位于其上三级。以下命令从仓库根目录执行；不要把多仓库工作空间根目录当成 Git 仓库。

## 先选择本次任务

- 所有修改先读根目录 [AGENTS.md](../../../AGENTS.md) 和 [开发规范](../../../docs/development-guide.md)。
- 验证、提交、PR、主线构建和远端同步，按 [提交与交付 SOP](../../../docs/contribution-sop.md) 执行。
- 涉及版本、安装包、Actions 或发版，再读 [版本与发布 SOP](../../../docs/release-sop.md)，以实际工作流和脚本核对执行结果。
- 流程文档是操作真值；本技能只提供路由和关键决策，不复制完整 SOP。发现文档与执行行为不一致时，核实后同步修正，不能静默绕过门禁。

## 交付决策

1. 检查工作目录、分支、远端和用户已有改动。本技能只适用于当前 Community 仓库；不修改邻近 Pro 或授权仓库。从更新的 `master` 建短分支，不覆盖他人工作。
2. 完成本次范围内的代码和对应验证。普通功能、修复或文档提交保持产品版本；仅明确切换交付阶段时用 `scripts/common/version_sync.py set/next` 同步五处元数据。IPC 版本单独评估。
3. 按本次变更选择验证。记录执行命令、结果、跳过原因与未验证边界；提交前至少通过版本一致性与差异空白检查。不要把未执行的检查标为通过。
4. 任务包含提交或交付时，检查暂存 diff，用中文 Conventional Commits 提交，在 GitHub 向 `master` 提 PR 并填写仓库模板。工作流通过后，再在任务授权范围内合入和同步；未要求提交的任务不因加载技能而自动提交。
5. 判断完成状态必须对应提交 SHA。PR 构建成功与合入后主线构建成功分别核对；安装包同时核对 `build-info` 与 SHA256。只有已运行的步骤才能报告完成。

## 必须保留的边界

- GitHub `origin` 承载 Actions；Gitee `gitee` 是代码副本。推送 Gitee 不会触发 GitHub 构建，同步不能强推或覆盖分叉历史。
- 日常 CI 只生成内部 dev 包，不改写源码版本、不创建 tag 或 Release。失败排查和重跑按发布 SOP，不通过反复提交无关改动触发重建。
- 用户明确要求发版时才准备 `community-vX.Y.Z[-beta.N]` annotated tag 与本仓库验收 Issue，运行手动 Release 工作流。技能本身不授权创建 tag、合并 PR 或发布 Release。
- Release Action 仅生成草稿。对外发布须满足目标阶段验收且有任务授权；缺少验收或签名时报告具体阻塞，不把 dev 改成 stable 来演练。不能覆盖既有版本或移动 tag。
- 不提交构建产物或私钥。正式包保留授权校验与签名门禁；runner smoke 不能当作真实桌面、干净机或算法效果验收。

## 结束时报告

说明实际改动、产品版本是否变化、验证结果、分支/提交/PR、对应 CI 与产物状态、尚待人工验收的事项。任务只要求本地修改时，报告本地结果即可，不额外触发远端发布。
