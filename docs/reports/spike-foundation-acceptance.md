---
title: Spike 分析线底座原型验收记录
doc_type: report
status: prototype
owner: BoundFree
last_updated: 2026-07-23
milestone: Spike（下一阶段独立产品）
---

# Spike 分析线底座原型验收记录

本记录覆盖 Spike 分析线的**底座打通原型**，用于验证「单仓库 / 双可执行（core + eeg + spike）」策略下，
共用底座能否无损承载高采样率（30 kHz 级）Spike 数据。此为原型，非发布件；不进入 v1 功能清单，
不改变 v1 = 公共底座 + EEG 的产品边界。

## 范围

- `bf_core` 共享静态库：从单体 `main_window` 依赖链中抽出控制器、IPC、授权、渲染、i18n、核心与通用 UI 控件，供 `bf_app`（EEG）与 `bf_spike` 双可执行复用。
- `bf_spike` 独立可执行入口（`native/src/app/spike_main.cpp`）：版本加载、i18n、命令行参数与 `--smoke-spike-open` 冒烟。
- Spike DSP 引擎：
  - 检测（`dsp/spike_detect.{h,cpp}`）：MAD 阈值 + 不应期，`snippetLen`/时间戳全部由 `fs` 推导。
  - Sorting（`dsp/spike_sort.{h,cpp}`）：PCA 降维 + K-Means 聚类。
- Spike 共享数据模型（`dsp/spike_types.h`）：`SpikeAnalysis` / `SpikeCluster`，作为分析层与渲染层的唯一载体。
- Spike 分析服务层（`controller/spike_analysis_service.{h,cpp}`）：后台线程 detect -> sort -> 组装 `SpikeAnalysis`，与 EEG `AnalysisService` 的线程与 latest-request-wins 模式一致。
- 4 个纯 QPainter 自绘视图（`ui/spike_widgets.{h,cpp}`）：簇平均波形、raster、PSTH、ISI；配色/边距/空状态与 `analysis_widgets` 保持一致。
- Spike 主窗口（`ui/spike_window.{h,cpp}`）：经 `SidecarClient` 打开文件、取片、组装请求并触发分析，`fs` 严格取自文件 metadata。
- Intan RHD/RHS 边车读取器（`sidecar/.../readers/intan_reader.py`，基于 `neo`）与 `reader.optional_dependency_missing` 结构化错误码（含 9 语言文案）。

## 底座复用性核验（关键）

策略成立的前提是底座对采样率不可知。核验方式：

- 代码审计：`bf_core`、DSP 与录制链路无硬编码采样率；`record_controller` 的 `chunkSamples <= 0` 时按 `fs * kDefaultChunkSeconds` 自适应。
- 运行核验：`bf_spike --smoke-spike-open` 以 30 kHz 合成信号跑通 检测 -> sorting -> 组装 -> 绘制；`fs=30000` 由 CSV metadata 流入，检测/请求配置中的 `fs` 均来自 metadata 而非常量。

## 验证命令

```bash
cmake -B native/build -S native
cmake --build native/build --target bf_app bf_spike spike_dsp_test
native/build/spike_dsp_test
QT_QPA_PLATFORM=offscreen native/build/bf_spike --smoke-spike-open <30kHz_spiky.csv>
python3 scripts/check_i18n.py
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q
```

## 结果

| 项目 | 结果 |
| --- | --- |
| `bf_core` 抽取后双可执行构建 | PASS：`bf_app`、`bf_spike` 均编译链接通过 |
| Spike DSP 单测 | PASS：detect n=6 / snippetLen=45；sort purity=1 / nClusters=2 |
| Spike 端到端冒烟（30 kHz 合成，注入 65 spike） | PASS：`spikes=67 clusters=3`，`fs` 自 metadata 流入未硬编码 |
| EEG 回归（系统 UI 冒烟） | PASS：`smoke-m8-system-ui-ok` |
| C++ 黄金回归（M3/M4） | PASS：M3 `max_abs_err=1.10e-11`、M4 `failures=0` |
| 全量 pytest | PASS：239 passed, 2 skipped |
| i18n 静态检查（含 native/src 禁中文） | PASS |

## 已知边界（未完成，不得视为发布就绪）

- **无真实 Intan 样本回归**：检测/sorting 仅以合成信号验证；`IntanReader` 未用真实 RHD/RHS 样本端到端验收，`neo` 为可选依赖，缺失时按结构化错误码降级。
- **无 sorting 数值黄金基线**：EEG 线各里程碑均有 SciPy 黄金比对，Spike sorting 尚无对应黄金数据与容差定义（PCA/K-Means 结果为原型级）。
- **无 Spike 视图性能门禁**：4 个视图为 QPainter 自绘，未做 GPU/大道数（高通道 x 高采样率）性能画像与门禁，等同 EEG 的 M2 GPU gate 尚未为 Spike 建立。
- **未整合底座标注/导出**：Spike 结果尚未接入回放标注与录制导出链路。
- **窗口复杂度**：`main_window.cpp` 仍为 God object（约 1.4k 行），`core_window` + `eeg_window` 拆分为待办技术债，`spike_window` 目前直接复用 `bf_core` 未走该拆分。
- 产品版本保持 `0.1.0-dev`；Spike 线正式进入需以 v1 stable 收口与上述门禁补齐为前提。
