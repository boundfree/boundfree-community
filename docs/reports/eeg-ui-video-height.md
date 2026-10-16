---
title: EEG 视频面板默认高度调整
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 视频面板默认高度调整

视频面板首次显示时以 150 逻辑像素为目标高度，受内容最小高度约束。窗口增高时优先扩展波形，避免视频继续按比例占据更多空间。仅首次展开初始化，保留手动拖动分隔条及后续显隐操作的尺寸选择。

已增量构建；`QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-video-height python3 -m pytest tests/native/test_fixed_layout.py tests/native/test_m6_video_sync.py -q` 结果 5 passed（5.17s）。检查了 900×560 视频展开截图。Windows 真机效果仍由用户实测，不将离屏结果作为真实显示验收。
