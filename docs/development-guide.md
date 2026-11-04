---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-11-02
---

# 开发规范

本文收口分支、版本、文档格式和验证规则。历史细节见 [`archive/git-development.md`](archive/git-development.md) 与 [`archive/documentation-style.md`](archive/documentation-style.md)。

日常操作按 [提交与交付 SOP](contribution-sop.md)；版本、构建与发版按 [版本与发布 SOP](release-sop.md)。代码代理使用仓库技能 [community-delivery](../.agents/skills/community-delivery/SKILL.md)，PR 使用仓库模板。修改流程时在同一 PR 同步受影响的入口、规范与模板，避免维护多套规则。

## License And Extension Boundaries

本仓库自有核心采用 Apache-2.0，当前 ICA 和基本结果导出属于核心；第三方内容保留原许可。完整范围、贡献条款和未来付费扩展规则统一见 [许可与扩展边界](../legal/LICENSING.md)。商业扩展放独立私有仓库，本仓库只接收开源核心和公开集成接口，不通过目录名或授权开关把已开源代码变成专有代码。

新增依赖或资源核对来源和许可；修改许可时同步根目录 `LICENSE` / `NOTICE` 与 `legal/` 中的打包副本，并运行发布资产检查。整机激活向扩展授权的迁移尚未实施，不能因许可调整宣称免费安装包已完成。

## Collaboration Platforms

仓库关系、权限和远端配置统一见 [提交 SOP 第 0 节](contribution-sop.md#0-两个托管地址与协作顺序)。

## Branch Model

本地开发与构建副本分工见 [工具链说明](toolchain-wsl2.md)：Linux/WSL 维护源码和 Git 提交，Windows 副本只接收单向同步并编译。切换布局时先保全全部未提交改动；不能将旧副本直接视为源码覆盖当前候选。

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

默认从已更新的 `origin/master` 分出；发现当前分支属于另一个未合入 PR 时，先识别依赖，不能把它静默当作主线。确有依赖的任务可以使用堆叠 PR，但必须注明前置 PR 和实际基线，前置任务合入前不得合入后续任务；步骤见 [提交 SOP 第 1 节](contribution-sop.md#1-开始修改)。

### 评审与合入

新增功能先在 GitHub Issue 明确问题、范围和验收标准；小型文档/维护修改可直接在 PR 说明。完成实现后，作者自查 diff、验证证据和验收项，再申请评审。多人协作时至少由一名有仓库 Write 或更高权限的非作者评审确认；维护者负责最终合入。未解决的阻塞意见、失败或未完成的必要 CI、未合入的前置依赖都阻止合入。

单人维护时仍使用 PR，由维护者对最新 head SHA 记录自审结论、验证证据与合入决定。GitHub 不允许作者 Approve 自己的 PR，因此不能虚构另一位审核者或把 AI 审查当成平台认可的他人批准；存在其他审查者时不以单人例外跳过其阻塞意见。已有任务授权可由代理继续执行，skill 本身不授予合入权限。

默认使用 **Create a merge commit**，保留功能分支提交与 PR 合入边界，不默认 squash 或 rebase merge。合并提交标题使用中文 Conventional Commits，正文关联 PR。更新已共享分支时采用 merge `origin/master`；不为整理历史强推分支。若仓库实际规则不允许 merge commit，先由维护者统一规则，不能由代理临时改换合入方式。

评审、必要 CI 和合入前核对都针对最新 head SHA 及当前主线。新增提交或更新基线后，重查受影响内容、评审意见与 CI；旧的批准和旧的绿色运行不自动代表新提交通过。`gh pr checks` 没有列出所需检查时，不能按“无失败”放行。

### 合入后的生命周期

合入后单独核对 `push / master` 的 CI 与包身份，通过后按提交 SOP 快进同步 Gitee。确认分支已合入、没有后续 PR 或其他工作目录依赖，才清理其远端和本地分支；不批量删除历史分支。合入后发现问题，在新的 `fix/*` 或 `hotfix/*` 分支修复，或经新 PR revert，不 reset/强推主线。

`hotfix/*` 默认也从最新 `origin/master` 创建并走相同评审与 CI；从旧 tag 建分支只用于复现或准备补丁，修复须经 PR 回到主线后才能按当前发布流程交付。本项目尚未建立旧版本独立维护线，不能把包含后续功能的主线包声称为仅有补丁的旧版包。发版使用 `master` 可达提交上的不可变 annotated tag，详见 [发布 SOP](release-sop.md)。

### 平台保护

上述是仓库协作规则，平台强制执行依赖实际套餐和设置。2026-10-25 核对：组织为 GitHub Free、仓库私有、`master` 的 `protected=false`，Ruleset 查询返回套餐限制的 403；目前不能声称已强制阻止直推。

具备平台能力后，由管理员在 **Settings → Rules / Branches** 配置 `master`：必须经 PR、两个平台的必要 CI 成功、分支与主线更新、审查对话解决、禁止强推和删除。Required status checks 选用实际成功 PR 中的检查名称。多人协作要求至少一次非作者批准，并使新增改动后的旧批准失效；单人维护时保留人工自审记录，不配置无人能够满足的他人批准人数。允许 merge commit 时不要同时要求 linear history。

配置后核对规则是否实际生效、是否允许绕过及适用范围；不得为启用保护擅自改变仓库可见性或订阅。套餐能力见 [GitHub 分支保护说明](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)，评审限制见 [GitHub PR 批准说明](https://docs.github.com/en/pull-requests/how-tos/review-pull-requests/approving-a-pull-request-with-required-reviews)。

## Version Rules

产品版本、构建标识与更新命令统一在 [版本与发布 SOP 第 1–3 节](release-sop.md#1-只记住三个状态) 维护。

## Release Flow

发布步骤、签名与验收要求统一在 [版本与发布 SOP](release-sop.md) 维护。

## Documentation Rules

当前有效文档集中在 `docs/` 根目录。`docs/archive/` 仅用于历史追溯。

目录索引只列当前入口；完成、失效或被替代的计划、逐次报告、旧设计和配图按类型进入 `archive/`。归档前先把操作说明、有效约束和未验收项收口到当前文档，保留历史证据与来源，不把归档等同于验收完成。只有实际存在未执行任务时再创建 `plans/`，不维护空目录或并行的旧计划树。移动文档同步修复链接以及脚本/测试中的路径引用。

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

按 [提交 SOP 第 2 节](contribution-sop.md#2-实现文档与本地验证) 选择检查与执行命令。声明通过必须有对应输出；本地未运行的检查在 PR 记录原因和云端结果。

性能结论必须记录环境、命令、样本和结果。不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。
