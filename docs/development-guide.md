---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# 开发规范

本文收口分支、版本、文档格式和验证规则。历史细节见 [`archive/git-development.md`](archive/git-development.md) 与 [`archive/documentation-style.md`](archive/documentation-style.md)。

## Branch Model

`master` 是唯一日常集成主线；功能使用短期 `feature/*`、`fix/*`、`docs/*` 分支，通过 PR 合入。历史 `release` 分支不再作为发版要求。紧急修复可从已发布 tag 创建 `hotfix/*`，再同步回 `master`。正式 tag 必须位于 `master` 可达的提交上。

## Version Rules

`version.json` 为唯一人工维护真值。采用 SemVer，默认只用三种状态：

| 状态 | 仓库版本示例 | 用途 |
| --- | --- | --- |
| dev | `0.2.2-dev` | 内部开发；CI 临时生成 `0.2.2-dev.158.1+g12345678` |
| beta | `0.2.2-beta.1` | 选定交付范围的小范围试用；继续修复后 `.2` |
| stable | `0.2.2` | 已完成约定范围验收的正式版本 |

普通功能开发、修复、测试、文档提交无需递增产品版本。`version.md` 从本规范起记录交付阶段，逐提交详情使用 Git；旧记录保留。PATCH 用于已发布版本的修复，MINOR 用于新的阶段能力，MAJOR 用于重大不兼容变化；`0.x` 表示产品仍在成型。首个完整、可持续交付版本考虑 `1.0.0`。`alpha/rc` 仅保留旧脚本兼容，不作为默认日常流程。

版本变化使用同步工具，一次更新五处元数据并新增阶段记录：

```bash
python3 scripts/common/version_sync.py set 0.2.2-beta.1 --type chore --summary "准备首轮试用"
python3 scripts/common/version_sync.py set 0.2.2 --type chore --summary "完成阶段验收并准备正式发布"
python3 scripts/common/version_sync.py next patch --type chore --summary "开启下一修复版本"
python3 scripts/common/version_sync.py next minor --type chore --summary "开启下一功能阶段"
python3 scripts/common/version_sync.py check
```

切换版本的提交应包含全部同步文件；普通提交只需保证校验通过。IPC 版本独立维护，仅协议不兼容变化时评估递增。产品版本各段上限为 Windows FileVersion 的 65535；完整 SemVer 与构建编号显示在 AppVersion / `version.json`，Win32 四段数值保留基础版本加 `.0`，不作为包的唯一标识。

## Release Flow

具体操作、门禁、下载位置和恢复方法见 [release-sop.md](release-sop.md)。`master` 每次推送和 PR 自动编译、回归并打包；产物存 GitHub Actions Artifacts，默认保留 30 天，不自动生成 Release。手动 Release Action 只接受 `community-vX.Y.Z[-beta.N]` annotated tag，重新测试并构建相应提交，然后生成草稿。公开发布由维护者检查后点击 Publish。

Windows 正式版必须签名，并完成干净 Windows 10/11、升级/卸载、桌面和授权兼容验收。自动化 runner 上安装/运行/卸载是回归证据，不能替代上述人工验收。`scripts/windows/build_release.ps1` 保留正式版签名门禁；证书缺失时应失败。历史 tag 不覆盖，构建和 Release 失败后按 SOP 处理。

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
- 普通提交不递增产品版本；交付阶段变化时用同步脚本更新五处版本元数据和阶段记录。
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
python3 scripts/common/version_sync.py check
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
scripts/wsl/verify_m1_foundation.sh
```

Python 边车变更应运行：

```bash
python -m pytest tests/sidecar
```

性能结论必须记录环境、命令、样本和结果。不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。
