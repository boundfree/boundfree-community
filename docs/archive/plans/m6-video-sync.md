---
title: M6 视频同步
doc_type: plan
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M6
archived_at: 2026-10-23
---

# M6 视频同步

## 目标

在不改变 M5 EEG 回放时钟主从关系的前提下，复刻旧版视频同步体验：视频加载、从属 EEG 位置、偏移对齐、空白段黑屏、倍速跟随和约 20 fps 的帧渲染节流。

## 当前基础

- M5 `PlaybackState` 已提供播放、暂停、seek、五档倍速、位置时钟与尾部归零。
- `MainWindow` 以 25 ms 定时器推进 EEG 位置，并通过 `refreshPlaybackControls()`集中刷新光标和位置控件。
- Qt 6.9.2 工具链已补齐 `Qt6Multimedia`；本地开发使用独立 conda-forge 环境，M8 发布前必须复核 Qt/FFmpeg 分发许可。
- 旧 Python/PySide6 版 `VideoPanel` 是行为参考，不是运行时依赖。

## 实现边界

### 视频同步状态

新增独立、无 UI 依赖的 `VideoSyncState`：

- 映射 `videoMs = (eegTime - offset) * 1000`。
- 视频有效范围内，EEG 播放时允许媒体播放器自播，仅在漂移绝对值大于 400 ms 时校正。
- EEG 暂停时同步暂停，并在偏差大于 50 ms 时定位目标帧。
- 偏移前或视频结束后进入黑屏；进入黑屏时暂停并复位到 0 ms。
- 播放倍速只接受 M5 已支持档位。

### 视频面板

新增 `VideoPanel`，使用 `QMediaPlayer + QAudioOutput + QVideoSink`：

- 加载 `mp4/avi/mov/mkv/wmv/m4v`。
- `QVideoSink::videoFrameChanged` 中先判断黑屏，再判断 50 ms 节流；通过后才执行 `QVideoFrame::toImage()`。
- 使用快速等比缩放显示帧；seek、暂停后强制放行下一帧。
- 偏移输入仅在点击“应用”后生效。
- 所有可见文案走现有 i18n 契约。

### 主窗口接入

- 视频面板作为可停靠面板接入主窗口。
- EEG seek、播放/暂停、位置 tick、倍速切换和文件切换均调用同一同步入口。
- 视频始终从属 EEG 时钟，不引入独立同步定时器或 `singleShot` 补偿。

## 不包含

- M7 数据片段导出、录制与 Session 实时源。
- M8 安装包与代码签名。
- BLE、自研硬件和真实设备采集。

## 风险与处理

- 媒体后端/编解码器差异：运行验证使用本机 ffmpeg 生成的短 MP4；格式选择器覆盖六类扩展名，但不得据单一 MP4 样本宣称所有容器均已解码验收。
- 帧回调主线程成本：在 `toImage()` 前丢弃节流帧，避免无效 GPU 回读。
- seek 与播放器内部时钟竞态：只使用 EEG tick 驱动的幂等 `syncTo()`，不使用延迟补偿。
- 视频时长异步可用：时长未知时不判定尾后空白；收到 `durationChanged` 后立即重新同步。

## 验收

1. 纯状态冒烟覆盖：偏移映射、范围内播放、400 ms 漂移阈值、暂停定位、前后空白、倍速。
2. 真实媒体冒烟覆盖：生成短 MP4，offscreen 加载，收到时长与有效帧，验证节流计数、暂停 seek、前后黑屏和倍速。
3. 文件 UI 冒烟覆盖：打开 EEG 与视频后，播放、seek、倍速、偏移应用不崩溃且状态摘要符合预期。
4. 构建、现有 M3/M4/M5 冒烟、Native/sidecar pytest、i18n 完整性、JSON 与 diff 检查均通过。
5. 验收通过后更新 `feature-parity-checklist.md`、路线图、入口文档和 M6 验收报告；产品版本保持 `0.1.0-dev`。
