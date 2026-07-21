---
title: M4 频谱分析实施计划
doc_type: plan
status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M4
---

# M4 频谱分析实施计划

M4 已完成。本文保留 M4 在 M3 处理后的脑电数据上实现频域与时频分析的实施范围；验证结果见 [`../reports/m4-spectral-acceptance.md`](../reports/m4-spectral-acceptance.md)。不包含播放、标注、视频、录制、导出、授权或硬件采集。

## Goal

提供并验收四个分析视图：PSD/FFT、STFT 时频图、频段功率趋势和双通道 MSC 相干性。所有数值与旧 Python/PySide6 参考实现的 SciPy 黄金文件对齐。

## Architecture

```text
M3 处理后数据 → AnalysisService（后台、请求代次） → 当前激活 tab 的渲染视图
```

- `native/src/dsp/spectral.*`：纯 C++ 算法，负责 FFT、Welch PSD、STFT、MSC 和频段积分；不得依赖 Qt 或文件读取。
- `native/src/controller/analysis_service.*`：后台调度，使用请求代次保证 latest-request-wins；文件或参数变化后旧结果不得覆盖当前结果。
- `native/src/render/*_view.*`：只渲染分析结果，不实现算法或文件读取。
- 文件数据仍经 Python 边车的 `open/get_slice` 获取；Native 不直接解析科研文件格式。

## Scope

- PSD：Welch/Hann、FFT 幅度谱、dB、最大频率、频段色块和功率摘要。
- STFT：Hann、固定内联 jet 色阶、双线性平滑、静态 colorbar 与频段文字注释。
- 频段趋势：按 epoch 计算 delta/theta/alpha/beta/gamma，支持后台进度与取消。
- 相干性：两个不同通道的 MSC、频段着色、0.5 阈值和频段均值摘要。
- 只计算当前激活 tab；同通道相干性返回结构化 `analysis.*` 错误并由 UI 本地化。

## Dependencies

- M3 DSP 与静态浏览已完成。
- FFT 后端已接入构建：使用许可安全的 `pocketfft` BSD 单头文件；未引入 FFTW、KFR 或其他 GPL 风险依赖。
- 黄金文件由 `../boundfree-eeg-upper-computer` 的 SciPy 参考实现生成。

## Acceptance Criteria

- PSD、STFT、频段趋势和 MSC 的频率轴、时间轴及数值在记录容差内与黄金文件一致。
- 后台计算不阻塞 UI；旧请求结果不可覆盖新请求。
- 当前 tab 以外不触发分析计算。
- `zh_CN`、`en_US`、`pseudo` 的新增 UI 文本通过 i18n 检查和 offscreen 冒烟。
- 验收已完成：`feature-parity-checklist.md` 已更新，结果写入 `reports/m4-spectral-acceptance.md`。
