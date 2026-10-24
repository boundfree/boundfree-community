---
title: EEG 界面修复阶段 4A：波形与逐通道参数
doc_type: report
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面修复阶段 4A：波形与逐通道参数

## 范围

按 `docs/plans/eeg-ui-recovery.md` 分阶段实施，旧 Python/PySide6 归档只读。由主代理统一集成、构建、版本同步和提交。

## 实现

- 默认启用逐通道自动幅值；只统计当前时间窗中的有限样本，按 min/max 中心和 15% 余量设置范围，最小半幅 5µV，变化超过原跨度 5% 才更新。新数据立即失效，连续播放最多每 2 秒刷新；按垂直可见通道懒计算。
- 保留手动幅值开关；自动模式禁用手动输入，状态栏明确显示自动幅值，不再误报统一 ±200µV。
- 波形独立纵向滚动条/滚轮，最小通道行高 110px；解除原绘制 32 通道上限。滚动改变绘制偏移，不重建显示数据缓存；标注与时间映射沿用同一绘图区。
- 右侧处理区增加逐通道带通：继承全局、显式关闭、显式启用。编辑完成才提交；要求 `0 < low < high < fs/2`，非法编辑保留草稿和行内提示，不写入实际处理状态。低采样率下允许修正默认越界值，也允许关闭。
- 普通刷新保留行控件身份和焦点；外部有效设置变更会清失效草稿，清除覆盖按钮同时取消草稿。新文件清除逐通道覆盖，保留全局处理设置。
- 保持现有 DSP 顺序（参考后滤波）和索引语义：双极输出 `i - (i+1)` 使用覆盖索引 i；末源通道覆盖在双极模式休眠，切回后仍可用。

## 验证

使用 WSL2 Qt6 构建和 `QT_QPA_PLATFORM=offscreen`；四语言/两尺寸截图复用阶段 3 smoke，人工复核自动幅值中文小窗口。

- configure 与完整 build 通过；后续主窗口增量构建通过。
- 新增 `waveform_channels_test` 覆盖 40 通道滚动、NaN/Inf/DC、当前时间窗、40 帧不重扫/不重建、手动恢复与隐藏后滚动边界。
- 新增 `--smoke-ui-channel-parameters` 覆盖自动/手动接线、10Hz 保留/60Hz 衰减与通道隔离、非法频率拒绝、继承/关闭、双极映射、新文件清除、低采样率可纠正草稿。
- 联合运行 `test_waveform_channels.py test_waveform_viewport.py test_channel_parameters.py test_fixed_layout.py test_bf_app.py test_continuous_file.py test_continuous_playback.py test_seek_preview_smoke.py test_m4_analysis_smoke.py test_m5_playback_smoke.py` 首轮 50 项通过、逐通道 smoke 因旧行延迟删除导致查找到旧控件失败；修复后针对逐通道、布局、真实 CSV、连续播放及 seek 的 5 项复跑全部通过（6.49s）。合计 51 项不同用例通过。
- 版本、i18n、JSON、差异空白检查通过。

本阶段版本为 `0.1.70-dev`，本地独立提交，未推送。

## 边界

显示模型仍会复制全部选中通道的预取缓存；并未完成高通道/高采样率内存优化，也未进行 Windows 桌面、高 DPI 或性能验收。频率范围校验针对新增逐行编辑，全局旧控件的输入约束未在本轮全面改造。图外属性和主题仍属于阶段 5。
