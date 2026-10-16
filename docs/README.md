---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-07-22
---

# 文档索引

本目录保存 BoundFree EEG Native 的当前核心文档和历史归档。日常开发只应优先阅读当前核心文档；`archive/` 下内容用于追溯旧计划和旧规范。

当前 v1 stable 的上位边界来自 `../BoundFree_EEG_System_Requirements_Spec.md`：v1 先完成通用 EEG/LFP/电生理文件分析，自研硬件采集闭环进入 v1.1/v2。

## 当前核心文档

| 文档 | 类型 | 用途 |
| --- | --- | --- |
| [`roadmap.md`](roadmap.md) | 路线图 | v1 边界、当前阶段、M0-M8 路线和阶段门禁 |
| [`plans/eeg-ui-recovery.md`](plans/eeg-ui-recovery.md) | 当前计划 | 旧版 UI 对齐、分析曲线与进度交互的分阶段修复 |
| [`plans/m8-packaging-release.md`](plans/m8-packaging-release.md) | 当前计划 | M8 冻结边车、安装布局、系统收口与 Windows 发布门禁 |
| [`contracts.md`](contracts.md) | 契约 | 版本源、IPC envelope、错误码、i18n 约束 |
| [`development-guide.md`](development-guide.md) | 开发规范 | 分支、版本、文档格式和验证规则 |
| [`toolchain-wsl2.md`](toolchain-wsl2.md) | 工具链 | WSL2 Qt6 工具链路径和构建命令 |
| [`architecture-design.md`](architecture-design.md) | 架构设计 | C++/Qt6 主程序与 Python 边车总体架构 |
| [`feature-parity-checklist.md`](feature-parity-checklist.md) | 验收清单 | 新旧上位机功能对齐和黄金文件验收条目 |

## 阶段验收记录

| 文档 | 作用 |
| --- | --- |
| [`reports/eeg-ui-stage2a-session.md`](reports/eeg-ui-stage2a-session.md) | 阶段 2A 读片会话复用、请求合并与错误恢复 |
| [`reports/eeg-ui-stage1b-seek.md`](reports/eeg-ui-stage1b-seek.md) | 阶段 1B 拖动预览、单次定位与状态隔离回归 |
| [`reports/eeg-ui-stage1a-spectrum.md`](reports/eeg-ui-stage1a-spectrum.md) | 阶段 1A 谱线缺失修复与曲线敏感回归 |
| [`reports/m2-format-acceptance.md`](reports/m2-format-acceptance.md) | 已验收与未实现文件格式的证据 |
| [`reports/m2-rendering-gpu-gate.md`](reports/m2-rendering-gpu-gate.md) | 真实 GPU/显示门禁记录 |
| [`reports/m3-dsp-acceptance.md`](reports/m3-dsp-acceptance.md) | M3 DSP 黄金比对与静态浏览接入记录 |
| [`reports/m4-spectral-acceptance.md`](reports/m4-spectral-acceptance.md) | M4 频谱算法、后台调度、渲染和黄金比对记录 |
| [`reports/m5-playback-annotations-acceptance.md`](reports/m5-playback-annotations-acceptance.md) | M5 回放状态、事件标注、CSV 往返和 UI 文件流记录 |
| [`reports/m6-video-sync-acceptance.md`](reports/m6-video-sync-acceptance.md) | M6 视频格式、同步状态、帧节流与主窗口文件流记录 |
| [`reports/m7-record-export-acceptance.md`](reports/m7-record-export-acceptance.md) | M7 录制、背压、三格式导出与回读验收记录 |
| [`reports/m8-packaging-release-acceptance.md`](reports/m8-packaging-release-acceptance.md) | M8 打包实现、Linux 可验证证据、Windows 自动化验收执行记录与未完成发布门禁 |

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

- 当前阶段计划必须在实施前创建；已完成阶段计划更新为 `completed`，并以 `reports/` 中验收记录作为交付证据。
- 已完成阶段的可交付结论以 `reports/` 中对应验收记录与测试结果为准。
- 版本、IPC、错误码和 i18n 契约优先更新 [`contracts.md`](contracts.md)。
- 提交级版本记录从规范启用提交开始维护在根目录 `version.md`；分支、版本号更新时机、文档格式和验证规则优先更新 [`development-guide.md`](development-guide.md)。
