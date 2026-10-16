---
title: EEG 阶段 1B 进度拖动预览验收
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 阶段 1B 进度拖动预览验收

## 修复范围

进度条拖动立即更新现有位置秒数框，不发起文件切片。拖动期间冻结 EEG 时钟并暂停从属视频，避免旧位置覆盖预览或触发预取；松手后一次 seek，重启计时基准并恢复原播放状态。键盘及轨道步进也发定位请求，程序性刷新阻断信号。切换文件取消旧拖动，清除旧预览。未改变时间框本来的直接输入语义。

## 验证

环境：WSL2 Linux、Qt6 conda 工具链、Qt offscreen。完整 CMake build 通过，最终修改另行增量构建 bf_app。

新增 `--smoke-ui-seek-preview` 使用真实 MainWindow/QSlider 与合成 100 秒、两通道文件状态，拦截窗口请求而不启动边车：

- 拖到 60% 立即显示 60 秒且没有读取请求。
- 同文件结果回调及 150ms 实际 Qt 播放计时器不覆盖预览、不触发预取。
- 松手精确请求 60 秒一次；实际键盘事件和轨道步进各请求一次。
- 程序刷新不触发请求，换文件取消旧拖动。

定向回归：`QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_seek_preview_smoke.py tests/native/test_m5_playback_smoke.py tests/native/test_spectrum_render.py tests/native/test_m4_analysis_smoke.py tests/native/test_bf_app.py -q`，46 passed。

视频状态/媒体既有回归：`QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_m6_video_sync.py -q`，3 passed。该测试不等同于真实鼠标拖动与视频播放的 Windows 桌面联合验收。

提交前执行 i18n 检查、version_sync check、JSON 校验、git diff --check。

## 后续边界

此阶段仅改善交互反馈与状态隔离。松手后的文件读取仍走既有边车重新启动/打开文件路径；仍有 UI 线程处理与控件重建，不能宣称读取延迟或播放整窗跳动已解决。工具栏布局仍待阶段 3，目标秒数框在窄窗口可能受既有工具栏溢出影响。Windows 实际桌面、显示缩放与连续拖动流畅度尚待验证；未生成安装包。
