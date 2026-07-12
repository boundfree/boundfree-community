---
title: 项目路线图
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-07-12
---

# 项目路线图

本文收口 BoundFree EEG Native 的阶段路线、M0 结论和关键门禁。详细历史计划保存在 [`archive/plans/`](archive/plans/)。

## 当前状态

当前版本为 `0.1.0-dev`。M0 技术验证已完成，结论为 conditional go：

- IPC：可进入 M1，初版采用 stdio + JSON 控制消息 + 长度前缀二进制 payload。
- 渲染：Xvfb/Mesa 软件 OpenGL 已验证代码路径，但真实 GPU 验收尚未完成。
- 旧版性能画像：MNE 打开文件延迟和 Python DSP 热点已确认，后续必须异步打开文件并迁移 DSP。

## v1 产品边界

v1 stable 定位为面向动物电生理科研场景的通用 EEG/LFP/电生理文件分析软件。正式实现是本仓库的 C++/Qt6 Native 版本；旧 Python/PySide6 版只作为参考实现、功能基线、Python 边车迁移来源和黄金文件生成器。

v1 stable 必须完整对齐旧版通用文件分析体验：文件打开与懒加载、多通道波形浏览、Raw/CAR/Bipolar 重参考、工频与谐波陷波、单通道带通、PSD、STFT、频段功率趋势、相干性、事件标注、视频文件同步、EDF/HDF5/CSV 片段导出、录制与 Session 骨架、离线机器授权、Windows 10/11 安装包、简体中文和英文界面。

v1 stable 不承诺自研硬件采集闭环。自研 `ADS1299-4 + ESP32-C3`、BLE 实时采集、真实设备录制、设备状态监控和 12 小时以上采集长稳测试进入 v1.1/v2。v1 仍保留数据源、Session、录制和实时管线扩展边界。

## 当前阶段

当前阶段进入 M2-3：真实 GPU/显示门禁、静态浏览交互加固和 M2 验收证据收口。M2-3 仍不代表完整 M2 验收；只有 `docs/reports/m2-rendering-gpu-gate.md` 记录真实 GPU/显示环境通过后，M2 波形浏览门禁才能关闭。

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
- DSP 使用 Eigen 与 pocketfft，避免 GPL 风险依赖。
- 功能不变以 [`feature-parity-checklist.md`](feature-parity-checklist.md) 为验收依据。
- 对外宣称支持的文件格式必须有真实样本和验收记录；没有事件样本时不得宣称事件支持完成。
