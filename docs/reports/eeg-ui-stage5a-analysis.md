---
title: EEG 界面修复阶段 5A：图外属性与分析状态
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 界面修复阶段 5A：图外属性与分析状态

## 实现与旧版依据

旧版 `spectrum_view.py`、`coherence_view.py`、`band_trend_view.py` 使用图外 QLabel / legend_row。按此结构，四个分析页使用独立标题和可滚动属性区显示状态、结果通道/时段、频带图例与摘要；属性区在小窗口可压缩，图形仍保留 160px 最小高度。绘图区保留坐标轴、刻度、网格和数据，移除原图内标题、趋势图例及误置于时间轴方向的 STFT 频带名称。

补充分析数值刻度、当前语言的数字格式、STFT 色条范围和功率单位。时频热图按真实 bin 中心的相邻中点计算边界，色块和刻度共享同一坐标范围；趋势只有一个 epoch 时绘制点。对短向量、缺轴、非有限值和不规则矩阵作安全防护。

四页分别维护 idle/pending/ready/empty/failed。新文件、定位和处理变化失效旧结果；失败、空结果清除旧图与摘要。结果关联 generation、页签、类型及原始通道/时段，过期响应不能回填。语言切换重译缓存结果和坐标，不发起 DSP。自动播放刷新遇 pending 任务等待完成，避免每 500ms 取消慢任务；用户参数变化和暂停仍可主动替换请求。

## 验证

主代理统一使用 WSL2 Qt6 与 `QT_QPA_PLATFORM=offscreen`。configure、完整 build 与主窗口增量构建通过。联合运行 `test_analysis_presentation.py test_spectrum_render.py test_fixed_layout.py test_bf_app.py test_continuous_file.py test_continuous_playback.py test_seek_preview_smoke.py test_channel_parameters.py`：**48 passed in 15.56s**。版本、i18n、JSON、差异空白检查通过。截图产物不进入仓库；主代理检查四页及中文小窗口，修正了属性高度、色条单位重叠等布局问题。

新增绘图测试覆盖图外标题、坐标语言、真实 bin 边界、单 bin/单 epoch、畸形模型。频谱敏感测试改为统计新增曲线颜色像素，排除数值刻度/网格改变造成的假阳性。

新增 `--smoke-ui-analysis-presentation` 使用真实计算服务验证四页成功→失败→空→恢复、外部属性不覆盖图、语言不重算、换文件清空、旧 generation 忽略和 pending 自动刷新保护。

版本为 `0.1.72-dev`，本地独立提交，未推送。

## 边界

单个 STFT bin 无相邻中心可推导宽度，使用中心 ±0.5 单位的显示范围；这是显示回退，不表示测得的分析分辨率。旧版亮暗主题在阶段 5B 单独对齐。离屏测试不替代 Windows 高 DPI、真实 GPU 或高通道负载验收。
