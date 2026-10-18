---
title: 波形与分析的原始和处理后数据模式
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-18
---

# 波形与分析的原始和处理后数据模式

`0.1.84-dev` 在波形工具栏提供“原始波形 / 处理后波形”切换，默认使用处理后数据。波形与分析输入同步切换，用户无需同时对照两种波形。

## 行为

原始模式跳过重参考和滤波，恢复输入通道名称和数量，并保留现有处理设置。处理后模式按已保留的重参考、陷波、全局带通及逐通道覆盖重新计算。逐通道覆盖按参考输出索引解释，原始模式暂禁用逐通道编辑，避免源通道与参考输出混淆。

切换时使旧分析任务失效，清除旧结果，并根据当前可见区间重新计算所选分析页。PSD/FFT、STFT、频段功率趋势、相干性共用选择后的数据输入，其余页切入时按需计算。播放预取与拖动预览同样使用选择后的数据来源。

## 验证

已在 WSL2 Qt6 工具链完成 `cmake --preset wsl2-qt6-conda` 和全构建。以下针对性回归通过，结果为 **7 passed in 9.94s**：

```bash
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_waveform_data_modes.py tests/native/test_channel_parameters.py tests/native/test_continuous_playback.py tests/native/test_scrub_preview.py tests/native/test_analysis_refresh.py -q
```

`check_i18n.py`、`version_sync.py check` 和 `git diff --check HEAD` 检查通过。

对应入口为 `tests/native/test_waveform_data_modes.py`，覆盖实际波形数值、参考后的通道名称、设置保留、五语言切换及控件宽度。连续播放、拖动预览、分析刷新和逐通道参数使用既有回归入口检查。

本次仅增加查看和分析的数据选择，片段导出继续采用既有原始数据语义。Windows 桌面人工验收及安装包重建不属于本次验证结果。
