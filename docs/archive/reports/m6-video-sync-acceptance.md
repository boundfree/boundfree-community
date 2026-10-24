---
title: M6 视频同步验收记录
doc_type: report
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M6
archived_at: 2026-10-23
---

# M6 视频同步验收记录

## 结论

M6 视频同步实现与自动化验收通过。视频由 M5 EEG 位置时钟驱动，不建立独立同步时钟或 `singleShot` 补偿；偏移、漂移校正、黑屏、倍速和帧节流均已接入主窗口。

## 实现范围

- `VideoSyncState`：`videoMs = (eegTime - offset) * 1000`；播放时漂移绝对值大于 400 ms 才 seek；暂停时偏差大于 50 ms 才 seek。
- `VideoPanel`：`QMediaPlayer + QAudioOutput + QVideoSink`，在 `toImage()` 前按 50 ms 阈值丢弃帧，最高约 20 fps。
- 偏移前和视频结束后暂停、复位并清空画面；重复黑屏同步不重复 seek。
- 偏移输入通过“应用”按钮显式生效。
- 主窗口的播放、暂停、seek、25 ms 位置 tick、倍速、文件切换均调用统一视频同步入口。
- EEG 与视频使用联合时长 `max(EEG duration, video offset + video duration)`。
- 所有视频面板文案已进入九个 locale 的 i18n 完整性检查。

## 文件格式证据

本机用 ffmpeg 4.4.2 生成 64×48、25 fps、1 秒确定性短视频，再由 Qt 6.9.2 Multimedia 的 FFmpeg 8.0.1 后端实际加载、取得 1.000 秒时长和有效帧。

```text
┌──────────┬────────────────────────┬──────────┬────────┬────────┐
│ 扩展名   │ 容器 / 视频编码        │ 时长     │ 有效帧 │ 结果   │
├──────────┼────────────────────────┼──────────┼────────┼────────┤
│ .mp4     │ MP4 / H.264            │ 1.000 s  │ 是     │ PASS   │
│ .avi     │ AVI / MPEG-4           │ 1.000 s  │ 是     │ PASS   │
│ .mov     │ MOV / MPEG-4           │ 1.000 s  │ 是     │ PASS   │
│ .mkv     │ Matroska / MPEG-4      │ 1.000 s  │ 是     │ PASS   │
│ .wmv     │ ASF / WMV2             │ 1.000 s  │ 是     │ PASS   │
│ .m4v     │ MP4 / MPEG-4           │ 1.000 s  │ 是     │ PASS   │
└──────────┴────────────────────────┴──────────┴────────┴────────┘
```

无容器的 MPEG-4 elementary stream 即使使用 `.m4v` 扩展名也不提供可靠时长，不属于可同步文件；带 MP4 容器的 `.m4v` 已通过。

## 验证结果

- CMake configure：PASS；Qt 6.9.2 Core/Widgets/Multimedia。
- CMake build：PASS；`bf_app`、`m3_dsp_test`、`m4_spectral_test`。
- `bf_app --smoke-m6-sync`：PASS；400/50 ms 阈值、偏移、前后黑屏、重复黑屏幂等、五档倍速。
- `bf_app --smoke-m6-video <path>`：六类容器均 PASS；每类取得 2 个渲染帧并在 `toImage()` 前丢弃至少 2 帧，证明节流路径执行。
- `bf_app --smoke-ui --open-file <csv> --open-video <mp4>`：PASS；EEG 文件与视频面板通过主窗口联合打开。
- `pytest tests/native/test_m6_video_sync.py -q`：PASS，3 项。
- `pytest tests/native tests/sidecar -q`：PASS，94 项。
- M3/M4 黄金二进制与 M5 回放 smoke：PASS。
- `python3 scripts/common/check_i18n.py`：PASS。

## 环境与边界

- 环境：WSL2 Linux，Qt 6.9.2，Qt Multimedia FFmpeg 8.0.1，`QT_QPA_PLATFORM=offscreen`。
- 本机没有 `DISPLAY` 或 `WAYLAND_DISPLAY`，且未安装可用的 Orca computer CLI；因此没有进行真实桌面视觉交互验收。M6 的媒体解码、帧回调、同步状态与主窗口文件流均已实际运行，但 UI 像素外观和真实音视频设备行为仍应在 M8 Windows 发布验收中复核。
- 当前 conda-forge 多媒体包元数据标为 GPL；该环境只用于本地开发验证，不是发布依赖。M8 必须选用许可已核对的 Qt/FFmpeg 分发并保留 LGPL 合规材料。
- M6 不包含 M7 录制与片段导出、M8 安装包与签名、BLE 或真实硬件采集。
