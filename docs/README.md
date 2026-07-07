---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-07-08
---

# 文档索引

本目录保存 BoundFree EEG Native 的当前核心文档和历史归档。日常开发只应优先阅读当前核心文档；`archive/` 下内容用于追溯旧计划和旧规范。

当前 v1 stable 的上位边界来自 `../BoundFree_EEG_System_Requirements_Spec.md`：v1 先完成通用 EEG/LFP/电生理文件分析，自研硬件采集闭环进入 v1.1/v2。

## 当前核心文档

| 文档 | 类型 | 用途 |
| --- | --- | --- |
| [`roadmap.md`](roadmap.md) | 路线图 | v1 边界、M0 结论、M1-M8 路线和阶段门禁 |
| [`phase-1-foundation.md`](phase-1-foundation.md) | 当前计划 | M1-A 基础契约与可启动骨架执行计划 |
| [`contracts.md`](contracts.md) | 契约 | 版本源、IPC envelope、错误码、i18n 约束 |
| [`development-guide.md`](development-guide.md) | 开发规范 | 分支、版本、文档格式和验证规则 |
| [`toolchain-wsl2.md`](toolchain-wsl2.md) | 工具链 | WSL2 Qt6 工具链路径和构建命令 |
| [`architecture-design.md`](architecture-design.md) | 架构设计 | C++/Qt6 主程序与 Python 边车总体架构 |
| [`feature-parity-checklist.md`](feature-parity-checklist.md) | 验收清单 | 新旧上位机功能对齐和黄金文件验收条目 |

## 历史归档

| 路径 | 用途 |
| --- | --- |
| [`archive/README.md`](archive/README.md) | 归档说明 |
| [`archive/plans/`](archive/plans/) | M0-M8 旧里程碑计划 |
| [`archive/git-development.md`](archive/git-development.md) | 旧 Git 开发与版本管理规范 |
| [`archive/documentation-style.md`](archive/documentation-style.md) | 旧文档格式规范 |
| [`superpowers/plans/`](superpowers/plans/) | 当前执行计划，不作为主规格入口 |

## 文档状态

| 状态 | 含义 |
| --- | --- |
| `draft` | 尚未确认，不能作为实施依据 |
| `active` | 当前有效，后续开发应遵循 |
| `approved` | 已评审确认的架构或决策 |
| `completed` | 对应工作已完成，仅作为记录和复盘依据 |
| `superseded` | 已被其他文档替代，保留用于追溯 |

## 维护规则

- 当前阶段实施内容优先更新 [`phase-1-foundation.md`](phase-1-foundation.md)。
- 版本、IPC、错误码和 i18n 契约优先更新 [`contracts.md`](contracts.md)。
- 分支、版本、文档格式和验证规则优先更新 [`development-guide.md`](development-guide.md)。
- 架构决策和功能对齐要求分别维护在 [`architecture-design.md`](architecture-design.md) 与 [`feature-parity-checklist.md`](feature-parity-checklist.md)。
