---
title: EEG 多语言控件与分析详情可读性
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 多语言控件与分析详情可读性

参考模式下拉在翻译文本填充前曾缓存空选项宽度。现改为随内容计算，翻译后更新最小宽度和父分组布局，弹出列表不省略文字。分析选择框、逐通道模式框也随文本调整，窄工具栏按组换行。覆盖目前可加载的四个稳定语言（简中、繁中、英文、日文）及 pseudo；新增词条同步九份 TS，未将其他未发布 locale 宣称为已验收。

分析属性区移除内部 QScrollArea 和 128px 高度上限。当前页面根据真实宽度计算文字高度，隐藏页面清除临时高度，更新传递到标签页、内嵌 dock 和分隔器。物理空间不足时提高窗口所需最小高度，保留图形最小高度和正常字号，不再强制所有内容挤进 900×560。

PSD 和相干的频段说明/长串数值合并为三列表格：频段、频率（Hz）、频带功率（µV²）或相干性。色标对应图中频段，数字右对齐、单位集中在表头、使用应用正常字号和单元格间距。去掉重复图例；允许复制表格文字。PSD 即使图形选择 dB，表中频带功率仍由原始线性谱积分，并以 µV² 标明，不对 dB 谱直接积分。

完整性回归同时检查文字的换行高度、表格结构/正常字号、文档最后一行，以及每一级父容器的边界，避免“控件已变高但被外层标签页裁掉”的假通过。现有分析状态回归等待 Qt 排队布局完成后再检查图外属性位置，固定布局回归接受因完整文本产生的必要最小高度。

## 验证结果

Qt6 全量构建通过。最终定向命令：

```bash
QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-readable-release python3 -m pytest tests/native/test_analysis_text.py tests/native/test_analysis_presentation.py tests/native/test_reference_labels.py tests/native/test_fixed_layout.py tests/native/test_empty_controls.py tests/native/test_channel_parameters.py tests/native/test_scrub_preview.py tests/native/test_continuous_playback.py tests/native/test_m6_video_sync.py -q
```

结果 **21 passed in 25.12s**。其中分析文字测试内部覆盖 5 locale × 4 分析页 × 2 宽度共 40 组合，并检查长语言切回简体中文后释放多余最小高度；参考模式测试在 Fusion/Windows Qt style 中各覆盖 5 locale × 2 主题 × 3 模式。它们不是 40 或 60 个独立 pytest 用例。

此前 67 项联合回归为 66 passed / 1 failed，失败在异步完成后立即检查尚未排队布局的图外属性；调整为布局稳定后检查。随后进一步加强的祖先边界检查发现并修复真实的标签页/分隔器裁切，且补上顶部与底部工具栏换行后的真实最小高度，避免与波形工作区重叠。最终 21 项结果包含这些修复；不将此前通过的弱边界检查冒充完整文字验收。

i18n、版本一致性和 diff 检查通过。机械布局扫描无命中，但不覆盖 Qt 实际渲染，不能替代上述运行时检查或 Windows 真机/DPI 验收。未推送远端，原有授权 smoke 与 Windows 打包未提交改动保持原状。
