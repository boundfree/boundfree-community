---
title: 开发规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-24
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

`master` 是唯一日常集成主线；功能使用短期 `feature/*`、`fix/*`、`docs/*` 分支，通过 PR 合入。历史 `release` 分支不再作为发版要求。紧急修复可从已发布 tag 创建 `hotfix/*`，再同步回 `master`。正式 tag 必须位于 `master` 可达的提交上。

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
