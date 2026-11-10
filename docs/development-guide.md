---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-11-10
---

# 开发规范

本文是规则：分支怎么分、提交怎么写、谁能合入、文档怎么写。操作步骤在 [提交与交付 SOP](contribution-sop.md)，版本与发版在 [发布 SOP](release-sop.md)。

## License And Extension Boundaries

本仓库自有核心采用 Apache-2.0，当前 ICA 和基本结果导出属于核心；第三方内容保留原许可。完整范围、贡献条款和未来付费扩展规则统一见 [许可与扩展边界](../legal/LICENSING.md)。商业扩展放独立私有仓库，本仓库只接收开源核心和公开集成接口，不通过目录名或授权开关把已开源代码变成专有代码。

新增依赖或资源核对来源和许可；修改许可时同步根目录 `LICENSE` / `NOTICE` 与 `legal/` 中的打包副本，并运行发布资产检查。整机激活向扩展授权的迁移尚未实施，不能因许可调整宣称免费安装包已完成。

## Branch Model

### 主线与任务分支

`master` 是唯一日常集成主线。所有源码、文档、skill、工作流和版本准备改动都在任务分支完成，经 GitHub PR 评审后合入；不在本地 `master` 开发，不直接向 GitHub `master` 推送开发提交，也不通过网页直接修改主线文件。Gitee `master` 仅按提交 SOP 快进镜像已验证的 GitHub 主线。默认不增加长期 `develop` 或 `release/*` 集成线；已有历史 `release` 分支仅保留追溯。

一个可独立验收的任务对应一个短分支和一个 PR；需要多次提交、返修或更新主线时继续使用同一分支。合入或关闭后，新任务从最新 `origin/master` 新建分支，不复用旧分支。

| 前缀 | 用途 | 示例 |
| --- | --- | --- |
| `feature/` | 新增功能 | `feature/123-channel-groups` |
| `fix/` | 缺陷修复 | `fix/124-export-time-axis` |
| `docs/` | 文档、SOP、skill | `docs/125-branch-management` |
| `chore/` | 构建、CI、依赖、版本阶段准备 | `chore/prepare-beta2` |
| `refactor/`、`perf/`、`test/` | 重构、性能、测试专项 | `test/126-edf-import` |
| `spike/` | 有明确结论与退出条件的技术验证 | `spike/127-reader-capability` |
| `hotfix/` | 已发布版本的紧急缺陷 | `hotfix/128-startup-crash` |

分支使用小写英文、数字和连字符，采用 `<前缀>/<Issue编号>-<简短主题>`；没有独立 Issue 的小型文档或维护任务可省略编号。示例编号不代表已有任务。分支前缀与提交 type 分别描述任务和单次改动，`feature/*` 内可包含 `feat`、`fix`、`test` 等提交。

任务之间有依赖时的堆叠 PR 做法见 [提交 SOP 第 3.2 节](contribution-sop.md#32-任务依赖另一个未合入的-pr)。

### 评审与合入

- 新增功能先在 GitHub Issue 明确范围和验收标准；小型文档或维护修改可直接在 PR 说明。
- 多人协作时，至少一名有 Write 权限的非作者评审确认。单人维护时仍走 PR，由维护者对最新 head SHA 记录自审结论。GitHub 不允许作者批准自己的 PR，所以不虚构审核者，也不把 AI 审查当成他人批准。
- 合入方式固定为 **Create a merge commit**，不 squash、不 rebase merge。合并提交标题用中文 Conventional Commits。
- 评审、CI 和合入前核对都针对最新 head SHA。新增提交或主线更新后，旧的批准和旧的绿色运行不再有效。
- 未解决的阻塞意见、失败或缺失的必要 CI、未合入的前置 PR，都阻止合入。
- 不为整理历史强推分支，不 reset 或强推主线。合入后发现问题走新 PR 修复或 revert。
- 发版使用 `master` 可达提交上的不可变 annotated tag，详见 [发布 SOP](release-sop.md)。没有旧版本独立维护线，`hotfix/*` 同样从最新 `origin/master` 建分支。

### 平台保护

上述是仓库协作规则。2026-11-05 起 `master` 已开启 GitHub 分支保护（**Settings → Branches**），对包括管理员在内的所有人生效：

- 必须经 PR 合入，禁止直推、强推和删除 `master`；
- `build / package (ubuntu-24.04)` 与 `build / package (windows-2022)` 必须成功，且 PR 分支须包含最新 `master`（主线前进后需先更新分支、等新一轮 CI）；
- 审查对话须全部解决；新增提交使旧批准失效。

单人维护，必需批准数为 0，评审以 PR 中的人工自审记录为准；多人协作后再调高批准数。仓库允许 merge commit，未要求 linear history。管理员不可绕过；确需紧急处理时由维护者在设置中临时关闭“Do not allow bypassing”，处理后立即恢复并在 PR 或 Issue 中记录。改 CI 任务名时同步更新必需检查名称，否则 PR 将无法合入。

调整规则后核对是否实际生效、是否允许绕过及适用范围；不得为启用保护擅自改变仓库可见性或订阅。套餐能力见 [GitHub 分支保护说明](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)，评审限制见 [GitHub PR 批准说明](https://docs.github.com/en/pull-requests/how-tos/review-pull-requests/approving-a-pull-request-with-required-reviews)。

## Documentation Rules

当前有效文档集中在 `docs/` 根目录，历史计划、报告和旧设计不保留在仓库，靠 Git 历史追溯。

目录索引只列当前入口；完成、失效或被替代的计划、逐次报告、旧设计和配图直接删除，靠 Git 历史追溯。删除前先把操作说明、有效约束和未验收项收口到当前文档，不把删除等同于验收完成。只有实际存在未执行任务时才临时创建计划，不维护空目录或并行的旧计划树。移动文档同步修复链接以及脚本/测试中的路径引用。

`docs/` 中当前 Markdown 文档要求（根目录约定文件、PR 模板和 skill 按各自格式维护）：

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

chore: 准备 0.2.2-beta.1 试用版本

test: 增加 M2 格式夹具与验收证据
```


### 禁止提交的内容

不要提交构建产物、安装包、私钥、授权 keygen 私有材料或临时环境目录。

## Verification Rules

按 [提交 SOP 第 2.2 节](contribution-sop.md#22-验证) 选择检查。声明通过必须有对应输出；本地未运行的检查在 PR 记录原因和云端结果。

性能结论必须记录环境、命令、样本和结果。不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。
