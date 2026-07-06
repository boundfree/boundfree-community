# Milestone 5：回放控制与事件标注 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M2（且最好 M3）完成后定稿。

**Goal:** 复刻历史回放（播放/暂停/seek/倍速/prefill 平滑起播）与事件标注（点击添加、列表管理、导入、CSV 导出、波形叠加）。

**Architecture:** `PlaybackController` 状态机 + 后台回放推流（经边车按块 get_slice 或本地缓冲）；`MarkerController` 管理标注；波形叠加标签复用 M2 渲染层。

**Tech Stack:** C++/Qt6、IPC、QTimer。

## Dependencies
- M2（波形与会话）。DSP（M3）就绪时回放叠加滤波更完整。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/controller/playback_controller.{h,cpp}` | play/pause/seek/speed、位置时钟、prefill、推流 |
| `native/controller/marker_controller.{h,cpp}` | Marker 增删、导入、CSV 导出 |
| `native/ui/history_panel.{h,cpp}` | 进度条/播放键/倍速/导出按钮 |
| `native/ui/marker_panel.{h,cpp}` | 标注列表 |
| `tests/native/` | 状态机与标注测试 |

## Tasks

### Task 1：回放状态机
- **Interfaces Produces:** `PlaybackController::play/pause/seek(t)/setSpeed(x)`、`currentTime()`、信号 `positionChanged(t)`/`finished()`。
- **Tests（TDD）:** seek 只保留最新窗口请求（过期切片不覆盖）；倍速 0.25/0.5/1/2/4×；播放到尾发 finished 并归零（等价旧版）。
- **DoD:** 状态迁移与旧版一致。

### Task 2：prefill 平滑起播
- **Deliverable:** play/seek 时预取 min(win,5s) 历史，`prependHistory` 平滑衔接。
- **Tests:** 起播首帧不空窗；prefill 数据正确拼接。

### Task 3：后台推流
- **Deliverable:** 回放按块从边车/缓冲取数，喂 DSP 实时路径再渲染；不阻塞 UI。
- **Tests:** offscreen 冒烟：播放推进 currentTime，波形滚动。
- **DoD:** 256ch 回放帧率达 M0 基线。

### Task 4：事件标注
- **Interfaces Produces:** `MarkerController::add(t,label)/remove/list()`、`exportCsv(path)`、`importFrom(meta.markers)`。
- **Tests:** 波形点击→输入→添加；顶通道显标签、颜色轮换（旧版踩过 scene 多次连接死循环，注意只连一次）；导入 MNE/Intan/CSV 侧车标注。
- **DoD:** 标注增删/导入/导出与旧版一致。

### Task 5：CSV 标注导出与回读
- **Deliverable:** 导出 `_markers.csv`；打开文件时经边车带回标注（open 响应含 markers）。
- **Tests:** 往返一致（time_s/label）。

## Acceptance Criteria
- 播放/暂停/seek/倍速/prefill 行为与旧版一致。
- 标注增删、三来源导入、CSV 导出、波形叠加与旧版一致。

## Parity Gate
- 勾验清单：播放控制、倍速、seek、prefill、标注添加/导入/导出/叠加。
