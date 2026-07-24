---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-23
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

`version.json` 是唯一产品版本真值；根目录 `version.md` 是提交级人工可读版本记录。每个合入 `master` 的有效提交都必须更新产品版本和 `version.md`，但不为每个提交创建 release tag。Git commit 标题只保留一行摘要，完整提交说明写入 `version.md`。

版本更新规则：

- `PATCH`：默认路径。每个普通功能、修复、文档、测试、重构或发布链路提交递增一次，例如 `0.1.41-dev` -> `0.1.42-dev`。
- `MINOR`：阶段性公开能力边界变化时递增，并将 `PATCH` 归零。适用于完成新的里程碑、增加新的产品线能力、引入新的文件/分析/录制/发布能力边界，或需要对外说明的功能阶段，例如 `0.1.41-dev` -> `0.2.0-dev`。
- `MAJOR`：正式稳定大版本或不兼容产品边界变化时递增，并将 `MINOR/PATCH` 归零。迁移期继续使用 `0.x.y`；第一个完整客户可用版本发布为 `1.0.0`。若出现不兼容的数据、授权、部署或 IPC 产品边界，也必须评估 MAJOR。
- `rc`：准备发布候选版时使用当前开发版本号切换为 `X.Y.Z-rc.N`，并设置 `release_channel: rc`。
- `stable`：正式发布时去掉 prerelease，设置 `release_channel: stable`，并只在 `release` 分支创建 annotated tag。
- `hotfix`：从已发布 tag 或 `release` 分支修复时递增 `PATCH`，并发布更高稳定版本；已发布 tag 不复用、不覆盖。

版本同步命令：

```bash
python3 scripts/version_sync.py next patch --type feat --summary "新增能力摘要" --scope native/src/...
python3 scripts/version_sync.py next minor --type feat --summary "完成阶段能力摘要"
python3 scripts/version_sync.py set 0.2.0-rc.1 --channel rc --type chore --summary "准备 0.2.0-rc.1"
python3 scripts/version_sync.py set 0.2.0 --channel stable --type chore --summary "发布 0.2.0"
python3 scripts/version_sync.py check
```

提交要求：功能改动、`version.json`、`version.md` 和受影响的镜像版本说明必须放在同一个提交中。Git commit 只写一行摘要，完整提交说明写入 `version.md`。旧历史提交以 `git log` 为准，不在 `version.md` 逐条回填。`version_sync.py check` 是提交前门禁。

产品版本与 `ipc_protocol_version` 分开维护。IPC 协议 envelope、字段语义、payload 布局或不兼容错误码契约变化时，必须评估是否增加 `ipc_protocol_version`。

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
10. 在 `master` 继续使用 `next patch` 或按下一阶段边界使用 `next minor` 开启后续开发版本。

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

提交应小步、可审查。**统一使用中文 Conventional Commits 格式**，subject 使用中文。

### 格式

```text
<type>: <中文摘要>

[可选正文]

[可选页脚]
```

规则：

- subject 使用中文，说明做了什么；不以句号结尾。
- subject 尽量控制在一行内；需要细节时写 body。
- body 可选；用中文说明动机、影响范围或为何不做某事。多段用空行分隔。
- 一次提交只做一类事；不要把无关改动混在同一提交。
- 每个有效提交必须包含对应的 `version.json` 和根目录 `version.md` 更新；修复同一提交时可 amend，不额外递增版本。
- 不写无信息量的 subject，例如 `更新`、`修复 bug`、`wip`、`update`。
- type 保持英文小写；subject 与 body 使用中文。

### 允许的 type

| type | 用途 |
| --- | --- |
| `feat` | 用户可见功能或能力 |
| `fix` | 缺陷修复 |
| `docs` | 仅文档 |
| `test` | 仅测试或测试夹具 |
| `chore` | 构建、忽略规则、版本周期、工具链等杂项 |
| `spike` | 技术验证与其记录 |
| `refactor` | 不改变对外行为的重构 |
| `perf` | 性能改进 |

需要时可用可选 scope：`feat(ipc): ...`、`fix(license): ...`。

### 示例

```text
feat: 增加边车握手协议

fix: 修复授权版本判断

docs: 收口 M1-A 文档

spike: 记录真实 GPU 复测结果

chore: 准备 0.2.0-rc.1 发布候选

test: 增加 M2 格式夹具与验收证据
```


### 禁止提交的内容

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

版本变更必须运行：

```bash
python3 scripts/version_sync.py check
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
