---
title: M5 回放与标注实施计划
doc_type: plan
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M5
archived_at: 2026-10-23
---

# M5 回放与标注实施计划

M5 已完成。本文保留已打开文件会话的文件回放与事件标注实施范围；验证结果见 [`../reports/m5-playback-annotations-acceptance.md`](../reports/m5-playback-annotations-acceptance.md)。不包含视频同步、录制、数据片段导出、离线授权或硬件采集。

## 目标

在不改变「Python 边车负责科研文件格式读写、Native 负责 UI 与会话协调」边界的前提下，对齐旧版以下通用分析体验：

- 播放/暂停、进度 seek、`0.25/0.5/1/2/4×` 倍速、平滑起播预取、最新窗口优先、文件尾部归零和 EEG 位置时钟。
- 从边车 `open` 响应载入内嵌或 CSV 侧车事件；波形顶部叠加标注标签与轮换色彩。
- 波形点击新增标注及标签输入；标注列表编辑、删除、清空。
- 标注 CSV 导出，并由同一 CSV 侧车读取路径完成往返验证。

## 设计

```text
Sidecar open(events) ─┐
                       ├─► MarkerStore ─► 标注列表 / WaveformWidget 叠加
Native 点击新增 ───────┘                    │
                                             └─► sidecar export_markers_csv

回放工具栏 ─► MainWindow 时钟 ─► latest-only get_slice ─► 波形窗口
                    │                      ▲
                    └─► cursor / 位置标签 ─┘
```

- `controller/playback_state.*`：纯状态机，限定合法倍速、位置、时长、运行态和尾部归零语义；不依赖 UI 或 IPC。
- `controller/marker_store.*`：标注数据、颜色序号、排序、编辑、删除及事件载入；不承载文件写入。
- `MainWindow`：持有 UI 时钟，在 GUI 线程更新光标；仅在预取阈值触发 `get_slice`，依靠现有 `FileOpenController` 代次抑制过期切片。
- `WaveformWidget`：只绘制光标和 marker 覆盖层；单次点击通过 signal 报告 EEG 时间。只画顶部通道标签，避免每通道重复和多次连接。
- Python 边车：新增 `export_markers_csv` 协议，把结构化 markers 写为 `{stem}_markers.csv`；Native 不直接写科研文件或事件侧车。

## 验收标准

- 状态机覆盖暂停、seek、五档倍速、尾部归零和位置钳制。
- 通过 offscreen smoke 验证播放时钟、预取请求、最新请求优先与尾部归零。
- CSV fixture 的 `_markers.csv` 自动载入；新增/编辑/删除标注可见于波形和列表。
- 通过边车导出后重新 `open`，事件的时间与标签一致。
- 所有新增用户可见文本走 i18n；`check_i18n.py` 通过。
- M5 验收记录、功能对齐清单和路线图在验证后同步更新。

上述标准已通过，具体命令与结果见 M5 验收记录。

## 非目标

- M6 视频从属同步。
- M7 EDF/HDF5/CSV 数据片段导出、录制、Session 实时源。
- M8 打包、签名和发布。
