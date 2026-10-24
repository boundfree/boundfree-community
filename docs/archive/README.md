---
title: 历史文档归档
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# 历史文档归档

这里保存旧设计、已完成或已被替代的计划、各轮验收快照和配图。日常入口在 [当前文档索引](../README.md)。归档保留事实来源，不表示未完成事项已验收，也不授权执行旧命令或旧任务。

| 目录 / 文件 | 内容 | 当前入口 |
| --- | --- | --- |
| [plans/](plans/README.md) | M0–M8、UI 修复、品牌集成、原 superpowers 任务级计划和旧路线图 | [产品范围与当前路线](../roadmap.md) |
| [design/](design) | 原架构快照、M2 设计草案、在线激活旧设计 | [当前架构](../architecture-design.md)、[契约](../contracts.md) |
| [reports/](reports) | 各阶段与各轮 UI/数值/安装包验收、原功能对齐清单 | [当前验收清单](../feature-parity-checklist.md) |
| [images/](images) | 历史界面提案配图 | [分析操作说明](../analysis-guide.md) |
| [git-development.md](git-development.md)、[documentation-style.md](documentation-style.md) | 旧提交、逐提交版本和文档约定 | [开发规范](../development-guide.md)、[交付 SOP](../contribution-sop.md) |

## 常用追溯

- [历史 GPU 基线与复测命令](reports/m2-rendering-gpu-gate.md)
- [离线分离实现、交互与时间边界修复记录](reports/offline-eeg-separation.md)
- [离线分离数值来源与对照](reports/offline-eeg-numerical-provenance.md)
- [原始/处理后数据模式与导出记录](reports/waveform-data-modes.md)
- [Windows 打包阶段记录](reports/m8-packaging-release-acceptance.md)
- [云端构建流程首轮实施记录](reports/community-release-automation.md)
- [保留的 Spike 原型记录](reports/spike-foundation-acceptance.md)

## 阅读边界

- `last_updated` 是原文时间；本轮归档以 `archived_at` 标记，`original_status` 保留原状态。`superseded` 表示它不再是当前操作入口，不推翻原测试数据。
- 历史正文保留当时的分支、版本、环境、失败、待办与工作区日志路径；其中部分外部文件不随本仓库分发。旧文的“当前”“下一步”和执行指令只属于当时上下文。
- 原 superpowers 计划与设计已分别并入 `plans/` 和 `design/`，不再维护另一套目录和实施流程。
- 原验收清单中的未完成项已收口到当前验收清单；不能因为计划归档而放宽正式版门禁。
