# Milestone 3：信号处理引擎 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M2 完成后定稿。**本里程碑是数值一致性的核心，强制黄金文件比对。**

**Goal:** 在 C++ 复刻重参考、工频陷波（含谐波）、单通道带通的实时与离线路径，数值对齐旧版 scipy 实现。

**Architecture:** `native/dsp/` 纯算法层，不依赖 Qt；SOS 双二阶级联（Direct Form II Transposed），实时带状态、离线零相位；滤波器设计系数在 C++ 复算并与 scipy 比对。

**Tech Stack:** C++17、Eigen、pocketfft（BSD）。**禁止 FFTW/KFR（GPL）。**

## Dependencies
- M2（有数据可喂）；可与 M2 并行开发 DSP 纯算法部分。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/dsp/biquad.{h,cpp}` | 单个/级联双二阶，状态推进（实时）与零相位（离线） |
| `native/dsp/filter_design.{h,cpp}` | Butterworth 带通、iirnotch → SOS 系数 |
| `native/dsp/reference.{h,cpp}` | raw/CAR/bipolar 重参考 |
| `native/dsp/pipeline.{h,cpp}` | ProcessingConfig + process_realtime/process_offline |
| `tests/dsp/` | 单元测试 + 黄金文件比对 |
| `tests/golden/` | 由参考仓库产生的标准输出 |

## Golden File 生成（前置任务）
用参考仓库 `.venv_build/bin/python` 对固定随机种子的输入跑 `ProcessingPipeline.process_offline/realtime`，把输入与输出存为 `.npy`/二进制到 `tests/golden/`，作为 C++ 比对基准。容差：绝对/相对误差记录在测试中（建议实时路径 rtol≤1e-5，离线 rtol≤1e-6）。

## Tasks

### Task 1：滤波器设计系数比对
- **Interfaces Produces:** `designBandpass(low, high, fs, order=4)->SOS`、`designNotch(freq, fs, q=30)->SOS`。
- **Tests（TDD）:** SOS 系数与 scipy `butter(...,'sos')`/`iirnotch→tf2sos` 逐系数比对（golden）。
- **DoD:** 系数在容差内一致。

### Task 2：双二阶级联（实时状态 + 离线零相位）
- **Interfaces Produces:** `Biquads::processRealtime(chunk, &state)`、`processOffline(data)`（前向-后向）。
- **Tests:** 分块喂入的实时结果与整段一致（状态跨块）；离线零相位与 `sosfiltfilt` golden 比对。
- **DoD:** 与旧版 `filters.py` 行为一致。

### Task 3：重参考
- **Interfaces Produces:** `applyReference(data, mode)`；bipolar 减一通道并重建通道名。
- **Tests:** CAR/bipolar 结果与旧版 `reference.py` golden 比对；bipolar 通道数/命名一致。

### Task 4：处理管线（陷波谐波 + 逐通道带通）
- **Interfaces Produces:** `ProcessingConfig{fs, nCh, refMode, notchFreqs[], bandpass[]}`、`Pipeline::rebuild/processRealtime/processOffline`。
- **Tests:** 端到端（重参考→陷波谐波→逐通道带通）与旧版 `pipeline.py` golden 比对；谐波倍数逻辑（基频×1..N，<nyquist）一致。
- **DoD:** 端到端数值在容差内对齐。

### Task 5：接入静态浏览
- **Deliverable:** M2 的静态切片经 pipeline 处理后再渲染；滤波参数 UI（陷波/谐波/参考/带通面板）驱动 rebuild。
- **Tests:** offscreen 冒烟：改参数触发重算与重绘。
- **DoD:** 滤波后波形与旧版视觉/数值一致。

## Acceptance Criteria
- 所有 DSP 路径通过黄金文件比对（容差已记录）。
- 陷波谐波、逐通道带通、三种参考行为与旧版一致。
- DSP 层零 Qt 依赖，可独立测试。

## Parity Gate
- 勾验清单：重参考、陷波（含谐波）、单通道带通、实时/离线一致性。
