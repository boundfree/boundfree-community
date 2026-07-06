---
title: 文档格式规范
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-06
---

# 文档格式规范

本文定义 `docs/` 目录内 Markdown 文档的基本格式。目标是让架构、计划、流程和验收资料可检索、可评审、可长期维护。

## 文件命名

- 文件名使用小写英文、数字和连字符，例如 `architecture-design.md`。
- 里程碑计划使用 `milestone-N-topic.md`，例如 `milestone-1-skeleton.md`。
- 目录索引使用 `README.md`。
- 不使用空格、中文标点或临时后缀，例如 `new.md`、`final.md`、`v2.md`。

## 文档头

每个 Markdown 文件必须以 YAML front matter 开头。

```yaml
---
title: 文档标题
doc_type: architecture
status: active
owner: BoundFree
last_updated: 2026-07-06
---
```

必填字段：

| 字段 | 说明 |
| --- | --- |
| `title` | 文档标题，应与一级标题语义一致 |
| `doc_type` | 文档类型：`architecture`、`plan`、`process`、`checklist`、`index`、`report` |
| `status` | 文档状态：`draft`、`active`、`approved`、`completed`、`superseded` |
| `owner` | 维护方，默认 `BoundFree` |
| `last_updated` | 最近一次内容变更日期，格式 `YYYY-MM-DD` |

可选字段：

| 字段 | 说明 |
| --- | --- |
| `milestone` | 里程碑编号，例如 `M1` |
| `reference_repo` | 参考仓库路径 |
| `related` | 相关文档列表 |
| `scope` | 文档适用范围 |

## 正文结构

- front matter 后必须有且只有一个一级标题。
- 一级标题后用简短段落说明文档用途。
- 主要章节使用二级标题。
- 表格用于规则、职责、状态、接口等结构化内容。
- 命令使用 fenced code block，并标注语言，例如 `bash`、`powershell`、`text`。
- 相对链接必须可从当前文件位置正确跳转。

## 语气和内容

- 使用正式、明确、可执行的表述。
- 避免口语化表达、临时备注和未确认判断。
- 不写没有归属的 `TODO`、`TBD`、`待定`。
- 如果确实存在未决事项，应写入“风险与待确认事项”，并明确负责人、触发条件和决策时间。
- 文档中的版本号、分支名、路径和命令必须使用代码格式。

## 计划文档要求

里程碑计划必须包含：

- `Goal`
- `Architecture`
- `Tech Stack`
- `Dependencies`
- `File Structure`
- `Tasks`
- `Acceptance Criteria`

已完成的里程碑应保留原计划，并通过报告或状态字段说明完成结果，不直接删除历史计划。

## 变更要求

- 修改架构设计时，同步检查里程碑计划是否需要更新。
- 修改版本或分支规则时，同步检查根目录 `README.md` 和 `AGENTS.md`。
- 修改验收清单时，同步检查相关测试计划和黄金文件要求。
