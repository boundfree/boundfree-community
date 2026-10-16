---
title: EEG 界面修复阶段 5B：旧版亮暗主题
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 界面修复阶段 5B：旧版亮暗主题

## 范围与实现

按只读归档 `boundfree_eeg/ui/theme.py` 的有效颜色恢复 EEG 主题：浅色中性灰界面、白色图底、`#333333` 轴字、`#1565C0` 谱线；深色 `#181828` 图底、`#DDE0FF` 轴字、`#00CFFF` 谱线。原生控件选择保留旧版紫色强调（浅 `#68237A` / 深 `#9B4DA8`）。保留已验收的控件间距和自适应布局，不照搬会改变小窗口最小尺寸的旧样式参数。

恢复旧版每组八种通道色；隐藏前置通道或纵向滚动不改变某个输出通道的颜色。主题切换更新已经存在的波形和四个分析图，不重算 DSP。图外图例、摘要、状态沿 Qt palette 同步，频带色继续使用既有科学频带定义。

新增 EEG 专用 `eegPalette/eegChannelColor`，共享的通用绘图 palette 保留给独立 Spike 产品线。没有改动数值计算、IPC 或录制/导出协议。

## 验证

主代理统一完整 build 通过。`QT_QPA_PLATFORM=offscreen` 联合运行 `test_analysis_presentation.py test_spectrum_render.py test_fixed_layout.py test_bf_app.py test_waveform_viewport.py test_waveform_channels.py test_channel_parameters.py test_continuous_file.py test_continuous_playback.py test_seek_preview_smoke.py test_record_form.py test_m4_analysis_smoke.py test_m5_playback_smoke.py test_m6_video_sync.py`：首次 **58 passed / 2 failed**。两项失败定位到通道颜色选择，显式索引选择修复后，针对波形视窗、通道颜色、四页分析、频谱曲线和双主题布局复跑 **7 passed in 5.94s**。合计 **60 项不同用例通过（分批执行）**。

版本同步、i18n、JSON、差异空白检查通过；布局机械扫描无输出项（原生 Qt 覆盖有限）。人工复核浅色大窗口、深色小窗口与深色 STFT：曲线颜色、图外属性、数值轴和色条单位可读。截图留在工作区 `build/eeg-ui-stage5/`，不提交仓库。本阶段版本 `0.1.73-dev`，本地独立提交，未推送。

新增覆盖：已存在波形切暗后的精确曲线颜色、隐藏前置行颜色稳定、四页结果切主题不重新计算、双主题 × 四语言 × 两尺寸布局。Windows 原生窗口、高 DPI 和真实显示环境仍需桌面验收。

## 交付边界

阶段 1–5 的计划软件实现已完成，不等同于 Windows 桌面体验完全验收，也不宣称与旧版逐像素一致。连续播放保留当前前向视窗语义；高通道数缓存复制、GUI 线程离线滤波成本、同步 IPC 析构等待等限制仍见各阶段报告。
