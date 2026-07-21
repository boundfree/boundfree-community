---
title: M4 频谱分析验收记录
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M4
---

# M4 频谱分析验收记录

M4 在 M3 处理后的静态窗口数据上提供 PSD/FFT、STFT、频段功率趋势和双通道 MSC 相干性。计算由 Native 后台服务执行；文件格式读取仍由 Python 边车负责。

## 范围

- `pocketfft` BSD 单头文件作为 FFT 后端；未引入 FFTW、KFR 或其他 GPL 风险依赖。
- FFT 幅度谱、Welch PSD、STFT、五频段功率积分和 MSC coherence。
- 分析停靠栏的四个 tab：PSD/FFT、STFT、频段趋势、相干性。
- `AnalysisService` 的后台计算、latest-request-wins、取消结果抑制和同通道相干性错误路径。
- `zh_CN`、`en_US`、`pseudo` 及其他保留 locale 的翻译键完整性。

## SciPy 对齐参数

黄金数据由 `scripts/generate_m4_golden.py` 生成，完整参数和容差见 `tests/golden/m4/meta.json`：

- FFT：`np.hanning` 对称窗，`abs(rfft) * 2 / window.sum()`，不去趋势。
- Welch：`scipy.signal.welch(window="hann", detrend="constant", scaling="density", nperseg=256, noverlap=128)`。
- STFT：`scipy.signal.spectrogram(window="hann", nperseg=256, noverlap=192, detrend="constant", scaling="density")`，输出 dB。
- MSC：`scipy.signal.coherence(window="hann", nperseg=256, detrend="constant")`。
- 频段功率：对线性 Welch PSD 的闭区间频点求和后乘以 `df`。

## 验证命令

```bash
python3 scripts/generate_m4_golden.py
cmake -B native/build -S native
cmake --build native/build --target bf_app m3_dsp_test m4_spectral_test
native/build/m4_spectral_test tests/golden/m4
native/build/m3_dsp_test tests/golden/m3
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m4-analysis
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m4-render
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen \
  python3 -m pytest tests/native/test_m4_spectral_binary.py \
  tests/native/test_m4_analysis_smoke.py tests/native/test_bf_app.py -q
python3 scripts/check_i18n.py
```

## 结果

| 项目 | 结果 |
| --- | --- |
| M4 黄金比对 | PASS：11/11，最大误差 `5.12e-13` |
| M3 回归黄金比对 | PASS |
| AnalysisService smoke | PASS：PSD、latest-request-wins、取消、同通道错误 |
| 三个自绘控件 offscreen 渲染 | PASS：频谱、时频图、趋势均产生数据像素 |
| Native/M4 pytest | PASS：41 passed |
| i18n 静态检查 | PASS |

## 已知边界

- M4 按当前静态窗口分析；完整文件回放、seek、倍速和标注仍属 M5。
- 频段趋势当前基于已加载窗口计算；跨整文件按需读取属于后续回放/分析扩展，不改变 M4 的静态浏览范围。
- M4 不实现视频、录制、导出、授权闭环、BLE 或真实硬件采集。
