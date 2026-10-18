---
title: 波形与片段导出的原始和处理后数据模式
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-18
---

# 波形与片段导出的原始和处理后数据模式

波形与分析的数据来源切换自 `0.1.84-dev` 引入；`0.1.85-dev` 增加片段导出的原始/处理后数据独立选择。此次扩展沿用现有 Qt 界面、主题与布局；用户确认仅需切换查看，未增加两种波形同时对照视图。

## 查看与分析

工具栏新增“数据”下拉，默认选择“处理后”。处理后数据使用当前重参考、陷波、全局带通及逐通道带通设置；选择“原始”时跳过重参考与滤波，波形和分析同步使用原始输入。

切换模式保留处理参数。原始模式恢复输入通道的名称和数量；切回处理后时恢复相应处理结果，包括双极参考后的通道数量与名称。原始模式禁用逐通道参数编辑，避免原始通道与参考后通道索引错配。

## 片段导出

导出弹窗一次设置开始时间、结束时间及数据来源，默认采用当前可见区间和当前显示模式。用户可独立修改导出来源，无需改变波形显示模式；区间须满足文件时长内的有效起止范围。

打开弹窗时捕获处理参数。处理后导出在 Native 后台线程复用现有 DSP：所选区间位于当前缓存上下文内时，先处理该上下文再裁剪，以保留当前显示的滤波边界语义；缓存外区间按所选片段处理。输出使用参考后的通道名，并保留采样率与区间内标注，标注时间重基准到导出片段起点。

新增 `export_samples` 通过二进制 IPC 传递 Native 已准备的样本，Python 边车负责 EDF/HDF5/CSV 编码及事件写入。原始导出的 `export_segment` 接口继续兼容，IPC 协议版本保持 `1`。导出完成信号在线程结束、控制器可接受下一次导出后发出，支持完成后立即连续导出。

## 验证记录

`0.1.84-dev` 的独立波形/分析提交已单独完成全构建及 7 项针对性回归（`7 passed in 9.94s`），包括数据模式、逐通道参数、连续回放、拖动预览和分析刷新。导出相关源码、测试及文案在后续提交引入。

本轮在 WSL2 Qt6 工具链完成 configure 与全构建，从 `native/` 执行：

```bash
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda -j4
```

从仓库根目录执行以下针对性回归，结果为 **66 passed in 12.28s**：

```bash
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_waveform_data_modes.py tests/native/test_waveform_export_modes.py tests/native/test_channel_parameters.py tests/native/test_continuous_playback.py tests/native/test_scrub_preview.py tests/native/test_analysis_refresh.py tests/native/test_m7_record_export_smoke.py tests/sidecar/test_export_samples.py tests/sidecar/test_m7_export.py tests/sidecar/test_m7_framing.py -q
```

关键覆盖如下：

- `raw`、`filtered`、`CAR`、`bipolar` 分别对单通道和三通道输入导出 EDF/HDF5/CSV，共 24 个输出，与 SciPy 独立计算结果比较。CSV/HDF5 使用 `atol=2e-5, rtol=1e-6`，EDF 使用 `atol=0.1, rtol=1e-6`；采样率、通道名、重基准标注一致，源文件 SHA-256 未改变。
- 界面中原始与处理后合成波形的实际数值、通道恢复、处理参数保留、逐通道编辑限制、导出来源独立选择及区间校验。
- 简中、英文、日文、繁中及 pseudo 语言切换，浅色与深色主题离屏截图检查；现有连续播放、拖动预览、分析刷新及录制导出相关回归。
- `export_samples` 元数据与二进制 payload 校验、三格式回读、无效请求后的帧同步和重复导出标注清理。

单通道双极参考沿用原有数据回退行为，并同步保留原始通道名，避免处理后导出时名称与数据行数不一致。M3 黄金回归 `native/build/m3_dsp_test tests/golden/m3` 通过，`failures=0`；离线处理最大绝对误差 `5.38258e-12`，实时处理最大绝对误差 `1.10237e-11`。

i18n、版本同步及 diff 检查通过。截图保存在本地忽略目录 `.impeccable/review/`，不作为文档的外部图片依赖。

## 独立收尾评审

处置结论：**通过，可交付本次改动。** 无未解决的实质评审问题。

| 评审项 | Verdict |
| --- | --- |
| 显示切换、分析联动与参数保留 | resolved |
| 导出独立选择及缓存上下文 | resolved |
| Native DSP、通道名与标注保留 | resolved |
| 浅色/深色界面可用性 | resolved |
| 单通道 Bipolar 导出 P1 | **resolved** |
| Windows 原生人工验证 | pending — 已披露限制 |

## 当前限制

处理后导出仍是受单切片 **256 MiB** 上限约束的内存处理，不提供流式全文件导出。输入样本单位须为 `uV`；EDF 保留 16 字符通道名限制与 ±3200 µV 量化范围。

上述 66 项为针对性回归与离屏界面检查。`0.1.86-dev` 合并前全量回归已通过，结果为 **336 passed、4 skipped（148.45s）**；既有 Windows 参数转发测试已在独立提交中对齐，详情和跳过原因见 [Windows 打包入口命名参数验证对齐](windows-build-entry-test-alignment.md)。跳过项不计为发布验收通过。Windows 桌面人工验收及安装包重建尚未执行。
