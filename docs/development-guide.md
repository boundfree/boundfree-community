---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-06
---

# 开发规范

本文收口分支、版本、文档格式和验证规则。历史细节见 [`archive/git-development.md`](archive/git-development.md) 与 [`archive/documentation-style.md`](archive/documentation-style.md)。

## Branch Model

- `master` 是日常开发集成分支，代表下一个版本的开发状态。
- `release` 是稳定发布分支，只保存已发布或准备发布的稳定提交。
- 正式发布 tag 只打在 `release` 分支。
- 当前 `release` 分支是占位稳定线；首个 `rc` 或 `stable` 版本前不从该分支打 tag。
- 普通开发从 `master` 切短分支：
  - `feature/*`：功能或阶段开发。
  - `fix/*`：开发期修复。
  - `spike/*`：技术验证。
  - `docs/*`：文档调整。
- 已发布版本紧急修复从 `release` 或正式 tag 切 `hotfix/*`。

当前阶段从 `master` 创建短分支：

```bash
git switch master
git switch -c feature/m4-spectral
```

## Version Rules

产品版本遵循 SemVer：

```text
MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
```

迁移期使用 `0.x.y`。第一个完整客户可用版本发布为 `1.0.0`。

`version.json` 是唯一人工维护版本源。普通 feature 分支不修改产品版本号，只有以下情况修改：

- 开启新版本周期，例如 `0.2.0-dev`。
- 准备发布候选版，例如 `0.2.0-rc.1`。
- 准备正式发布，例如 `0.2.0` 和 `release_channel: stable`。
- 发布 hotfix，例如 `0.2.1`。

产品版本与 `ipc_protocol_version` 分开维护。IPC 协议 envelope、字段语义或不兼容错误码契约变化时，必须评估是否增加 `ipc_protocol_version`。

## Release Flow

正式发布流程：

1. 确认 `master` 已包含要发布的全部提交。
2. 从 `master` 创建 `release-prep/vX.Y.Z`。
3. 修改 `version.json` 为 `X.Y.Z-rc.N` 和 `release_channel: rc`。
4. 构建安装包并完成 QA。
5. QA 通过后修改为 `X.Y.Z` 和 `release_channel: stable`。
6. 合并到 `release`。
7. 在 `release` 上创建 annotated tag，例如 `vX.Y.Z`。
8. 从 tag 构建发布包。
9. 将发布提交同步回 `master`。
10. 在 `master` 开启下一个开发版本。

已经公开发布的 tag 不复用、不覆盖。需要修复时发布更高 patch 版本。

M8 起构建和签名使用根目录 `build_release.ps1`。`rc/stable` 必须签署并验证 `bf_app.exe`、冻结边车和最终安装包；`-SkipSigning` 只允许 `dev/alpha/beta`。Windows x64 构建、Inno Setup 安装、干净 Windows 10/11 全流程、正式授权公钥兼容和 `signtool verify /pa` 未通过前，不得进入 `rc` 或 `stable`。

## Documentation Rules

当前有效文档集中在 `docs/` 根目录。`docs/archive/` 仅用于历史追溯。

Markdown 文档要求：

- 文件名使用小写英文、数字和连字符。
- 每个 Markdown 文件以 YAML front matter 开头。
- front matter 必填字段：`title`、`doc_type`、`status`、`owner`、`last_updated`。
- front matter 后必须有且只有一个一级标题。
- 路径、分支名、版本号和命令使用代码格式。
- 不写没有归属的 `TODO`、`TBD` 或临时备注。
- 文档变更应同步更新 [`docs/README.md`](README.md)。

front matter 示例：

```yaml
---
title: 文档标题
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-06
---
```

`doc_type` 可使用：`architecture`、`plan`、`process`、`checklist`、`index`、`report`。

`status` 可使用：`draft`、`active`、`approved`、`completed`、`superseded`。

## Commit Rules

提交应小步、可审查。推荐前缀：

```text
feat: 增加边车握手协议
fix: 修复授权版本判断
docs: 收口 M1-A 文档
spike: 记录真实 GPU 复测结果
chore: 开启 0.2.0 开发周期
```

不要提交构建产物、安装包、私钥、授权 keygen 私有材料或临时环境目录。

## Verification Rules

声称完成前必须运行对应验证命令并检查输出。

文档变更至少运行：

```bash
git diff --check HEAD
```

JSON 变更至少运行：

```bash
python3 -m json.tool version.json
```

C++/Qt6 工程变更应运行：

```bash
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda
```

以上命令从 `native/` 目录执行。

当前 WSL2 开发环境使用稳定工具链入口 `/home/vsonic12138/workspace/BoundFree_Workspace/toolchains/qt6`。该路径指向实际 Qt6/conda 环境，避免在日常命令中直接依赖 conda 内部路径。

M1-A 基础验证可运行：

```bash
scripts/verify_m1_foundation.sh
```

Python 边车变更应运行：

```bash
python -m pytest tests/sidecar
```

性能结论必须记录环境、命令、样本和结果。不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。
