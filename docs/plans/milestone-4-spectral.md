# Milestone 4：频谱分析四视图 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M3 完成后定稿。强制黄金文件比对。

**Goal:** 复刻 PSD、STFT 时频图、频段功率趋势、相干性四个分析视图，计算跑后台线程（无 GIL 真并行），只算当前可见 tab。

**Architecture:** `native/dsp/spectral.*` 纯算法（Welch/spectrogram/coherence/band power）；`native/controller/analysis_service.*` 后台线程调度，只保留最新请求；四个 `native/render/*_view.*` 只渲染结果。

**Tech Stack:** C++/Qt6、Eigen、pocketfft（BSD）、QtConcurrent/QThread。

## Dependencies
- M3（DSP/滤波就绪，谱分析基于滤波后数据）。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/dsp/spectral.{h,cpp}` | Welch PSD、FFT 幅度谱、STFT、MSC coherence、band power |
| `native/controller/analysis_service.{h,cpp}` | 后台计算，latest-request-wins，进度信号 |
| `native/render/spectrum_view.*` | PSD 曲线 + dB 切换 + 频段色块 |
| `native/render/spectrogram_view.*` | STFT 图像（jet 色阶）+ colorbar + 频段文字注释 |
| `native/render/band_trend_view.*` | 频段功率趋势 + 色块图例 |
| `native/render/coherence_view.*` | 相干性曲线 + 频段着色 + 0.5 阈值 + 均值摘要 |
| `tests/dsp/`, `tests/golden/` | 比对 |

## Tasks

### Task 1：谱算法 + 黄金比对
- **Interfaces Produces:** `welchPsd(sig, fs, nperseg, asDb)`、`stft(sig, fs, nperseg)`、`coherenceMsc(a, b, fs, nperseg)`、`bandPower(psd, freqs, band)`、`BANDS/BAND_COLORS` 常量。
- **Tests（TDD）:** 与旧版 `spectrum.py` + scipy `spectrogram`/`coherence` golden 比对（容差记录）。
- **DoD:** 频率轴、功率值、相干值在容差内一致。

### Task 2：后台分析服务
- **Interfaces Produces:** `AnalysisService::requestStft/requestCoherence/requestBandTrend`、信号 `done(AnalysisResult{kind,payload})`/`progress(int)`/`failed(code)`。
- **Tests:** 断言计算在工作线程；只发最新请求结果；取消旧请求不覆盖新窗口（等价旧版）。
- **DoD:** 隐藏 tab 不触发计算。

### Task 3：PSD 视图
- **Deliverable:** Welch/FFT 模式切换、dB 切换、频段色块背景、频率上限调节。
- **Tests:** offscreen 冒烟 + 与旧版视觉对齐。

### Task 4：STFT 时频视图
- **Deliverable:** jet 色阶（内联定义，不依赖外部 colormap）、双线性平滑、静态 colorbar、图下频段文字注释。
- **Tests:** 渲染不崩溃；色阶/注释与旧版一致（旧版此处踩过 colormap 打包坑，注意内联定义）。

### Task 5：频段趋势视图
- **Deliverable:** 后台计算 + 进度、δθαβγ 色块图例、切换文件先中止旧计算。
- **Tests:** 切换数据源时旧计算被中止（等价旧版 abort worker）。

### Task 6：相干性视图
- **Deliverable:** 通道 A/B 选择、MSC 曲线、频段背景着色、0.5 阈值线、各频段均值摘要；同通道给 error_code 提示。
- **Tests:** 与旧版 `coherence_view` 行为/数值一致。

## Acceptance Criteria
- 四视图数值通过黄金比对。
- 计算在后台、只算当前 tab、latest-request-wins。
- 视觉呈现（色阶/图例/注释/阈值）与旧版一致。

## Parity Gate
- 勾验清单：PSD、STFT、频段趋势、相干性。
