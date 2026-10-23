---
title: 项目路线图
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# 项目路线图

本文收口 BoundFree EEG Native 的阶段路线、M0 结论和关键门禁。详细历史计划保存在 [`archive/plans/`](archive/plans/)。

## 当前状态

当前版本以根目录 `version.json` 为准（按交付阶段维护，日常构建编号由 CI 生成）。M0 技术验证已完成：

- IPC：stdio + JSON 控制消息 + 长度前缀二进制 payload 已用于 M2 的文件 `open/get_slice` 路径。
- 渲染：真实 Windows GPU/显示验收已通过（见 [`reports/m2-rendering-gpu-gate.md`](reports/m2-rendering-gpu-gate.md)）。
- 旧版性能画像：MNE 打开文件延迟和 Python DSP 热点已确认；M2 异步打开和 M3 DSP 迁移已完成，M4 继续以旧版为频谱黄金来源。

## 产品分线与阶段策略

BoundFree EEG 按信号范式分为两条产品线，共享同一套底座，不拆分为两个独立仓库：

- **公共底座（core）**：文件打开与懒加载、IPC 与 Python 边车、波形浏览与渲染、事件标注、视频同步、片段导出、录制与 Session 骨架、离线授权、i18n、打包与发布。EEG 与 Spike 两线共用，占整体代码约九成。
- **EEG 分析线（低频 EEG/LFP/电生理）**：重参考、工频与谐波陷波、带通、PSD、STFT、频段功率趋势、相干性等频谱范式。
- **Spike 分析线（高频神经元放电）**：高采样率数据源与存储、阈值检测、spike sorting、波形簇叠加、raster/PSTH、ISI/自相关等事件范式。

阶段策略：**第一版本（v1）只交付「公共底座 + EEG 分析线」**，作为可发布的通用 EEG/LFP 文件分析软件。Spike 分析线作为**下一阶段的独立产品面孔**（可为独立 SKU / 可执行程序），复用同一底座，通过可插拔的数据源、分析层与渲染层接入，不重做一套软件。

## v1 产品边界

v1 stable 定位为面向动物电生理科研场景的通用 EEG/LFP/电生理文件分析软件，即上文「公共底座 + EEG 分析线」。正式实现是本仓库的 C++/Qt6 Native 版本；旧 Python/PySide6 版只作为参考实现、功能基线、Python 边车迁移来源和黄金文件生成器。

v1 stable 必须完整对齐旧版通用文件分析体验：文件打开与懒加载、多通道波形浏览、Raw/CAR/Bipolar 重参考、工频与谐波陷波、单通道带通、PSD、STFT、频段功率趋势、相干性、事件标注、视频文件同步、EDF/HDF5/CSV 片段导出、录制与 Session 骨架、离线机器授权、Windows 10/11 安装包、简体中文和英文界面。

v1 stable 不承诺 Spike sorting 分析线，也不承诺自研硬件采集闭环：

- **Spike 分析线**（高采样率 spike sorting、raster/PSTH 等）进入下一阶段独立产品线。
- **自研硬件采集闭环**（`ADS1299-4 + ESP32-C3`、BLE 实时采集、真实设备录制、设备状态监控、12 小时以上采集长稳测试）进入 v1.1/v2。

v1 在架构上必须保持底座的可扩展边界——数据源抽象、Session、录制与实时管线、分析层与渲染层接口，使后续 Spike 分析线与硬件采集接入时复用 v1 已建立的浏览、分析、标注、视频同步和导出流程，避免推倒重来。为此，v1 底座中与采样率相关的量（窗口点数、抽稀桶大小、切片粒度、payload 缓冲）不得按 EEG 的 1 kHz 量级写死，须按「窗口秒数 × 采样率」或「每像素目标点数」推导，天然兼容高采样率 Spike 数据。

## 当前阶段

M2-5 已收口：CSV、HDF5、EDF/BDF、BrainVision、EEGLAB 与 FIF 已有验收证据；Intan RHS 与 BBK/DAT/LSD 仍未实现。
M3 DSP 算法层与静态浏览离线处理已落地：重参考、陷波、带通和黄金文件比对通过。当前 UI 仅提供全局带通开关。
M4 频谱分析、M5 回放标注、M6 视频同步与 M7 录制导出均已完成。M8 打包实现和 WSL2 Linux 可验证部分已完成（见 [`reports/m8-packaging-release-acceptance.md`](reports/m8-packaging-release-acceptance.md)）；Windows x64 构建、Inno Setup 安装包、组织证书签名与干净 Windows 10/11 全流程仍是发布硬门禁。产品仍处于开发/试用阶段，未经门禁验收不得标为 `stable`。版本和云端构建发布操作见 [`release-sop.md`](release-sop.md)。

Spike 分析线已完成**底座打通的原型**（非发布件）：`bf_core` 共享静态库已从单体拆出，`bf_spike` 独立可执行入口、Spike 分析服务层（检测→sorting→组装 `SpikeAnalysis` 后台线程）、4 个纯 QPainter 视图（簇波形/raster/PSTH/ISI）与共享数据模型 `dsp/spike_types.h` 均已落地并可编译运行；Spike 检测（MAD 阈值+不应期）与 sorting（PCA+K-Means）DSP 引擎带单测（purity=1）。端到端冒烟以 30 kHz 合成信号验证 `fs` 全程来自文件 metadata、未硬编码。**仍缺**：真实 Intan 样本回归（当前仅合成信号）、Spike 视图的 GPU/大道数性能门禁、sorting 数值黄金基线、与底座标注/导出的整合。详见 [`reports/spike-foundation-acceptance.md`](reports/spike-foundation-acceptance.md)。

## 阶段路线

| 阶段 | 目标 | 进入条件 | 完成标准 |
| --- | --- | --- | --- |
| M0 | 技术验证 | 无 | IPC go、渲染 conditional go、旧版性能画像完成 |
| M1-A | 基础契约与可启动骨架 | M0 完成 | 主程序可启动、可显示版本、可切语言、可与边车握手 |
| M1-B | 授权骨架与 IPC 扩展 | M1-A 完成 | 授权接口与旧版语义对齐，IPC 支持 open/get_slice 的正式错误路径 |
| M2 | 打开文件与波形静态浏览 | M1 完成，真实 GPU 复测通过 | 各格式可打开，静态波形浏览、通道显隐、时间窗、主题可用 |
| M3 | DSP 与滤波数值对齐 | M2 完成 | 重参考、陷波、带通实时和离线路径通过黄金文件比对 |
| M4 | 频谱分析 | M3 完成 | PSD、STFT、频段趋势、相干性对齐旧版 |
| M5 | 回放与标注 | M2 完成，DSP 就绪更佳 | 播放、seek、倍速、prefill、标注导入导出对齐旧版 |
| M6 | 视频同步 | M5 完成 | 视频从属 EEG 时钟、偏移、黑屏和倍速跟随对齐旧版 |
| M7 | 录制与导出 | M3 和 M5 完成 | 双格式录制、背压、片段导出和标注写入对齐旧版 |
| M8 | 打包与发布验收 | M1-M7 完成 | Windows 10/11 安装包、边车打包、代码签名、全量功能清单勾验 |
| Spike 分析线（下一阶段独立产品）*[底座原型已落地]* | 高采样率数据源与存储、spike sorting、波形簇/raster/PSTH | v1 stable 完成，底座扩展点稳定 | 高频文件打开与浏览、检测/sorting/发放率分析、复用底座标注与导出 |
| v1.1/v2 | 自研硬件采集闭环 | v1 stable 完成，设备协议和样机样本就绪 | BLE 真实设备采集、设备状态、真实采集录制和 12 小时以上长稳测试 |

## M2 前硬门禁

进入 M2 波形浏览前，必须在真实 Windows 或 Linux GPU/显示环境复测 M0 渲染 spike，并将记录写入 [`reports/m2-rendering-gpu-gate.md`](reports/m2-rendering-gpu-gate.md)：

- 64/128/256 通道。
- 1 kHz 采样率。
- 4 s 可见窗口。
- 记录 FPS、CPU/GPU 占用和滚动、缩放、通道显隐主观流畅度。

验收目标：

- 256ch @ 1 kHz，4 s 窗口，优先达到 `>=55 FPS`。
- 如果只能达到 `>=30 FPS`，必须确认交互仍然平滑。
- 如果低于 `30 FPS`，先修正渲染方案，再进入 M2。

## 长期约束

- 文件读写只经 Python 边车，主程序不直接解析科研文件格式。
- 边车和后端只返回结构化错误，不返回本地化文案。
- EEG 数据统一为 `[n_channels, n_samples]`，单位为 `uV`。
- M3 滤波当前为纯 C++ 实现；M4 FFT 采用许可安全后端（计划为 `pocketfft`），避免 GPL 风险依赖。
- 功能不变以 [`feature-parity-checklist.md`](feature-parity-checklist.md) 为验收依据。
- 对外宣称支持的文件格式必须有真实样本和验收记录；没有事件样本时不得宣称事件支持完成。
