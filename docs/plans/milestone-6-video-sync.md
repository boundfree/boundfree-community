# Milestone 6：视频同步 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M5 完成后定稿。**这是旧版踩坑最多的模块，严格复刻既有解决方案。**

**Goal:** 复刻视频从属 EEG 时钟的同步：视频跟随 EEG 时间轴回放，偏移对齐，空白段黑屏，渲染节流。

**Architecture:** `VideoPanel` 用 Qt6 Multimedia（`QMediaPlayer` + `QVideoSink`）逐帧取图贴到显示控件；视频**绝不**用自己时钟独立播放——由 EEG 位置时钟每拍调 `syncTo(eegTime, playing)` 校正。

**Tech Stack:** C++/Qt6（Multimedia/MultimediaWidgets）。

## Dependencies
- M5（回放位置时钟）。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/ui/video_panel.{h,cpp}` | 加载视频、逐帧渲染、syncTo、offset、黑屏 |
| `tests/native/` | 同步逻辑测试（可 mock 时钟） |

## 关键约定（逐条来自旧版已解决 bug）
- 视频是 EEG 时钟从属；`syncTo(eegTime, playing)` 每 ~200ms 调：范围内让视频自播、漂移 >400ms 才 `setPosition` 校正；范围外走黑屏。
- 偏移 = 视频起点对应的 EEG 时间；`videoMs = (eeg - offset) * 1000`。
- 逐帧渲染节流到 ~20fps（距上次 <50ms 直接 return，连取图都不做）+ 快速缩放变换（避免 60fps 逐帧压垮主线程）。
- 空白段（偏移前/已结束）置 `blank` 标志，帧回调直接 return → 黑屏（不渲染第 0 帧封面）。
- 偏移用「应用」按钮显式触发，不在每次击键 seek。

## Tasks

### Task 1：视频加载与逐帧渲染
- **Interfaces Produces:** `VideoPanel::load(path)`、`isLoaded()`、信号 `durationKnown(sec)`。
- **Tests:** offscreen 冒烟：加载常见格式（mp4/avi/mov/mkv/wmv/m4v）不崩溃。
- **DoD:** 帧回调节流到 ~20fps。

### Task 2：syncTo 从属时钟
- **Interfaces Produces:** `VideoPanel::syncTo(eegTime, playing)`、`setRate(speed)`。
- **Tests（TDD，mock 时钟）:** 范围内自播、漂移>400ms 校正、范围外黑屏；倍速跟随。
- **DoD:** 无独立时钟、无 singleShot 补偿（旧版竞态根因）。

### Task 3：偏移对齐与黑屏
- **Interfaces Produces:** `VideoPanel::applyOffset(eegTime, playing)`、`offset()`。
- **Tests:** 偏移空白段黑屏（不显封面）；总时长 = max(EEG, 视频偏移末端)。
- **DoD:** 边输入偏移边跳转不黑屏（用「应用」按钮）。

## Acceptance Criteria
- 视频始终从属 EEG 时钟，回放/暂停/seek/倍速/偏移下同步正确。
- 空白段黑屏、渲染节流，无卡死/竞态。

## Parity Gate
- 勾验清单：视频加载、跟随回放、偏移对齐、空白段黑屏、倍速跟随。
