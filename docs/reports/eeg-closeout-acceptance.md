---
title: EEG v1 本环境收口验收记录
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-07-24
milestone: EEG closeout (pre-release)
---

# EEG v1 本环境收口验收记录

本记录覆盖「先做完 EEG」阶段：把已造好的零件装上车、补齐系统级收口项与旧版 bug 回归防护。
目标不是宣称 v1 stable 可发布，而是把**本环境可达的 EEG 对齐项全部闭合**，仅剩需真实环境的发布门禁。

## 本轮交付

| 提交 | 内容 |
| --- | --- |
| `a755cf6` | 通道/标注/分析三 dock 接入 `CollapsiblePanel` |
| `66ae0b3` | 旧版 8 条已修 bug 回归防护（12 用例） |
| `3f61bf9` | 每通道带通覆盖（`ProcessingViewState::perChannelBandpass`） |
| `2d1e276` | UI 数值展示接入 `I18nService` 区域格式化 |

## 闭合的 checklist 项

- 可折叠面板（通道/滤波/标注 dock）
- 状态栏文件名 + 模式标签（idle/playback/recording）
- i18n 数字/日期/单位区域格式化（状态栏 `file_window` 因 pytest 逐字节断言维持 C-locale）
- 每通道带通覆盖（缺省回落全局）
- M2 回放/静态双模式（M5 已交付，清掉陈旧勾选项）
- 旧版 8 条 bug 回归防护（6 条 full、2 条 partial，见下）

## 验证结果（独立复跑）

```text
┌──────────────────────────────┬────────────────────────────────────┐
│ 场景                         │ 结果                               │
├──────────────────────────────┼────────────────────────────────────┤
│ bf_app / bf_spike 构建       │ PASS                               │
│ M3 / M4 / Spike 黄金比对     │ PASS                               │
│ processing_view_state_test   │ PASS（全局回落 + override + clear）│
│ collapsible / i18n_locale    │ PASS                               │
│ i18n 静态检查                │ PASS                               │
│ --smoke-m8-system-ui         │ smoke-m8-system-ui-ok              │
│ --smoke-processing-offline   │ smoke-processing-offline-ok        │
│ --smoke-m5-playback          │ smoke-m5-playback-ok               │
│ bug regression tests (12)    │ 12 passed                          │
│ 全量 pytest                  │ 252 passed, 2 skipped              │
│ native/src CJK 扫描          │ CLEAN                              │
└──────────────────────────────┴────────────────────────────────────┘
```

## Bug 回归 partial 说明

1. **标注 scene 只连一次**：数据层颜色确定性已钉住；UI 层 `markerRequested` 连接次数尚无专用 smoke。
2. **切换不同 path 断开旧会话**：控制器析构与同路径二次请求已覆盖；跨 pathA→pathB 的专用 smoke 尚无。

两者均有明确后续入口建议，不阻塞本环境 EEG 收口结论。

## 仍未闭合（需真实环境 / 明确非本环境范围）

- 真实格式样本：Intan RHS、BBK/DAT、Open Ephys、Plexon、Blackrock
- 离线授权：正式公钥 + 旧版真实 key 兼容
- Windows：Inno Setup 安装/覆盖/卸载、代码签名
- v1.1/v2 硬件采集闭环

## 结论

EEG 侧在本 WSL/Linux 开发环境能验证的功能对齐项已全部闭合。
v1 当前状态 = **本环境可达项全绿 + 仅剩真实环境发布门禁**。
不得据此将产品版本标为 `rc` 或 `stable`。
