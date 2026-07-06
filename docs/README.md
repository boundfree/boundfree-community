---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-07-06
---

# 文档索引

本目录保存 BoundFree EEG Native 的架构、开发流程、里程碑计划和验收资料。所有 Markdown 文档应遵循 [`documentation-style.md`](documentation-style.md)。

## 核心文档

| 文档 | 类型 | 用途 |
| --- | --- | --- |
| [`architecture-design.md`](architecture-design.md) | 架构设计 | C++/Qt6 主程序与 Python 边车的总体架构、边界和关键决策 |
| [`git-development.md`](git-development.md) | 开发流程 | 分支模型、版本号更新、发布与 hotfix 规则 |
| [`feature-parity-checklist.md`](feature-parity-checklist.md) | 验收清单 | 新旧上位机功能对齐和黄金文件验收条目 |
| [`documentation-style.md`](documentation-style.md) | 文档规范 | Markdown 元数据、标题、状态和链接规则 |
| [`plans/README.md`](plans/README.md) | 计划索引 | M0-M8 里程碑计划入口 |

## 文档状态

| 状态 | 含义 |
| --- | --- |
| `draft` | 尚未确认，不能作为实施依据 |
| `active` | 当前有效，后续开发应遵循 |
| `approved` | 已评审确认的架构或决策 |
| `completed` | 对应工作已完成，仅作为记录和复盘依据 |
| `superseded` | 已被其他文档替代，保留用于追溯 |

## 维护规则

- 文档变更应和相关代码或流程变更同提交，避免实现与说明漂移。
- 架构决策、分支规范、版本号规则和验收标准的修改必须更新对应文档。
- 新增里程碑或任务计划时，必须同步更新 [`plans/README.md`](plans/README.md)。
- 对旧 Python 仓库的引用仅用于参考实现、性能基线和黄金文件来源。
