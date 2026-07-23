---
title: Spike 分析线底座原型验收记录
doc_type: report
status: prototype
owner: BoundFree
last_updated: 2026-07-24
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
cmake --build native/build --target spike_golden_test m3_dsp_test m4_spectral_test
native/build/spike_dsp_test
native/build/spike_golden_test tests/golden/spike
python3 scripts/generate_spike_golden.py   # 生成器可复现（md5 稳定）
QT_QPA_PLATFORM=offscreen native/build/bf_spike --smoke-spike-open <30kHz_spiky.csv>
QT_QPA_PLATFORM=offscreen PYTHONPATH=sidecar native/build/bf_spike --smoke-spike-export <in.csv> <out.csv>
python3 scripts/check_i18n.py
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q
```

## 黄金方案说明（Spike）

Spike sorting 用手写 Jacobi 特征分解 + 自写确定性 PRNG（seed=42），无法与 sklearn 逐位对齐，因此采用两层黄金：

1. **结构正确性校验**（每次运行都断言）：`meta.json` 记录合成信号的 ground-truth 注入峰索引与模板标签；测试断言检测匹配率 ≥95%（±2 样本容差）、sorting 簇纯度 ≥0.8。
2. **确定性自洽回归黄金**：C++ 引擎首跑冻结自身输出（detect_timestamps / cluster_sizes / sort_labels），后续逐位比对（maxAbsErr=0、diffs=0 为通过），防止算法回归。

生成器 `scripts/generate_spike_golden.py` 固定 numpy seed=20260724，产出可复现（`input_signal.npy` / `meta.json` md5 稳定）。

## 结果

| 项目 | 结果 |
| --- | --- |
| `bf_core` 抽取后双可执行构建 | PASS：`bf_app`、`bf_spike` 均编译链接通过 |
| Spike DSP 单测 | PASS：detect n=6 / snippetLen=45；sort purity=1 / nClusters=2 |
| Spike 端到端冒烟（30 kHz 合成，注入 65 spike） | PASS：`spikes=67 clusters=3`，`fs` 自 metadata 流入未硬编码 |
| EEG 回归（系统 UI 冒烟） | PASS：`smoke-m8-system-ui-ok` |
| C++ 黄金回归（M3/M4） | PASS：M3 `max_abs_err=1.10e-11`、M4 `failures=0` |
| Spike sorting/检测黄金回归 | PASS：`spike_golden_test` 结构校验 detect 20/20、纯度 1.0，回归逐位一致（maxAbsErr=0、labels diffs=0） |
| Spike 结果导出（事件 CSV） | PASS：真实 30 kHz 数据导出 67 行，列 `cluster_id,time_s`，与检测数一致 |
| EEG 主窗口处理面板拆分 | PASS：`main_window.cpp` 1439→1329 行，抽出 `eeg_processing_panel`；public API 零改动，EEG 回归全绿 |
| 全量 pytest | PASS：240 passed, 2 skipped |
| i18n 静态检查（含 native/src 禁中文） | PASS |

## 已知边界（未完成，不得视为发布就绪）

- **无真实 Intan 样本回归**：检测/sorting 仅以合成信号验证；`IntanReader` 未用真实 RHD/RHS 样本端到端验收，`neo` 为可选依赖，缺失时按结构化错误码降级。
- **无 Spike 视图性能门禁**：4 个视图为 QPainter 自绘，未做 GPU/大道数（高通道 x 高采样率）性能画像与门禁，等同 EEG 的 M2 GPU gate 尚未为 Spike 建立。
- **标注/录制未整合**：Spike 结果已可导出为事件 CSV（`cluster_id,time_s`），但尚未接入回放标注叠加与录制链路。
- **窗口复杂度**：已抽出 `eeg_processing_panel`（1439→1329 行），但 `main_window.cpp` 仍偏大，播放/marker/视频/导出/录制协调逻辑待继续外移；`spike_window` 直接复用 `bf_core`。
- 产品版本保持 `0.1.0-dev`；Spike 线正式进入需以 v1 stable 收口与上述门禁补齐为前提。
