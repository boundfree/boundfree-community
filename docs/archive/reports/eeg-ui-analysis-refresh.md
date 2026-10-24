---
title: EEG 播放期间分析区稳定刷新
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 播放期间分析区稳定刷新

原播放自动分析约每 500ms 发起一次请求，每次都先清曲线/摘要、显示 Pending，再恢复 Ready 和频段表格。这反复改变文字、表格显隐和详情区高度，是用户所见闪烁的直接代码原因。

显示缓存 `analysisPages_` 与待计算上下文 `pendingAnalysis_` 分离。已有有效结果时，后台请求保留其曲线、表格、Ready 状态及其原始取样时间；请求完成后在同一个绘制更新批次中替换结果与时间范围。不是把旧数值配上新请求的时间范围。新增独立 pending 标志保证耗时分析仍可完成，不因显示保持 Ready 而被下一次 500ms tick 反复取消。

频段表格采用稳定的 25/35/40 百分比列宽，减少数值位数变化导致的横向移动。保留正常字号、图外完整详情和原来的分析频率。明确换通道、参数、定位、处理或文件时仍清除不再适用的旧结果；空结果、无效数据和失败仍显示原有空/错误状态，不用旧结果掩盖失败。

新增真实播放定时器回归，通过 generation 属性事件同步取消 worker 来稳定保持后台请求，不依赖 FFT 恰好足够慢。等待超过刷新周期时检查旧曲线像素、摘要、状态、时间与几何不变，播放时间仍推进；注入新结果检查摘要和采样对齐时间同时更新；覆盖后台等待时切语言不重算/不混上下文、旧成功/失败不覆盖新操作、显式修改参数仍正常 Pending。

## 验证

`cmake --build --preset wsl2-qt6-conda -j 4 --target bf_app` 通过。最终命令：

```bash
QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_analysis_refresh.py tests/native/test_analysis_presentation.py tests/native/test_analysis_text.py tests/native/test_fixed_layout.py tests/native/test_scrub_preview.py tests/native/test_continuous_playback.py tests/native/test_continuous_file.py tests/native/test_m4_analysis_smoke.py -q
```

结果 **12 passed in 17.83s**。包括后台慢计算时保留画面、语言/旧回调/参数变化，以及已有四分析页、全文字、多语言布局、真实 CSV 读片、拖动和连续播放。i18n、版本一致性、JSON 和 diff 检查通过。静态复审未见阻断。

本轮修复定期清空与布局切换的成因，不宣称 Windows 真机或所有 GPU 上的视觉流畅度已验收；用户仍可通过开发版实际播放核对效果。没有更改波形/DSP算法或 IPC。未推送，已有未提交授权 smoke 与 Windows 打包修改保持原状。
