---
title: 实施计划索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-07-06
---

# 实施计划索引（Roadmap）

本目录把 [`../architecture-design.md`](../architecture-design.md) 拆分为可执行的分里程碑计划。

## 规划方式：滚动式（Rolling-Wave）

这是一次跨月的原生重写，且 **Milestone 0 是设计验证闸门**——渲染方案与 IPC 方案的最终细节
取决于 M0 spike 的实测结果。因此：

- **Milestone 0** 已拆成完全可执行的 bite-sized 步骤（下一步就做）。
- **M1–M8** 给出任务级计划：目标、依赖、文件结构、接口、任务清单、测试与验收（DoD）。
  每个里程碑的**最细粒度 2–5 分钟步骤**，在其前置里程碑通过后、正式开工前定稿——
  避免在工程尚未搭建、M0 结论未出时写下会返工的伪细节。

## 里程碑总览

| # | 计划文件 | 目标 | 前置 |
| --- | --- | --- | --- |
| M0 | [`milestone-0-spike.md`](milestone-0-spike.md) | 验证 256ch GPU 渲染 + C++↔Python IPC，并对旧版做性能画像 | 无 |
| M1 | [`milestone-1-skeleton.md`](milestone-1-skeleton.md) | 工程骨架：CMake/vcpkg、Qt 主窗口、边车进程管理、IPC 打通、i18n 骨架、授权校验 | M0 通过 |
| M2 | [`milestone-2-open-and-browse.md`](milestone-2-open-and-browse.md) | 打开各格式 + GPU 波形静态浏览、通道显隐、时间窗、主题 | M1 |
| M3 | [`milestone-3-dsp.md`](milestone-3-dsp.md) | 重参考/陷波（含谐波）/带通，实时与离线路径 + 黄金文件比对 | M2 |
| M4 | [`milestone-4-spectral.md`](milestone-4-spectral.md) | PSD/STFT/频段趋势/相干性四分析 tab，后台计算 | M3 |
| M5 | [`milestone-5-playback-markers.md`](milestone-5-playback-markers.md) | 播放/暂停/seek/倍速/prefill，事件标注与导入 | M2（DSP 就绪更佳） |
| M6 | [`milestone-6-video-sync.md`](milestone-6-video-sync.md) | 视频从属 EEG 时钟的 sync_to 逻辑复刻 | M5 |
| M7 | [`milestone-7-record-export.md`](milestone-7-record-export.md) | 经边车双格式录制（含背压）与片段导出（含标注） | M3、M5 |
| M8 | [`milestone-8-packaging-acceptance.md`](milestone-8-packaging-acceptance.md) | 边车 PyInstaller + Inno Setup 安装包、全量功能对齐、代码签名 | M1–M7 |

## 贯穿所有里程碑的全局约束

以下约束隐含适用于每个里程碑的每个任务（逐字取自设计文档）：

- 主程序语言/框架：**C++ / Qt 6**；构建 **CMake + vcpkg**。
- 文件格式读写一律经 **Python 边车**（MNE/pyedflib/h5py/pandas）；主程序不解析文件格式。
- IPC：控制/元数据 **JSON over 本地 socket/stdio**；批量 EEG 数组 **长度前缀二进制**，
  必要时升级 **共享内存**。
- DSP：**Eigen + pocketfft（BSD）**；**禁止 FFTW/KFR（GPL）**。
- EEG 数据 `[n_channels, n_samples]`，单位 **μV**；实时滤波用带状态 biquad，离线用零相位。
- 渲染：自研 **QOpenGLWidget** 折线渲染，仅上传可见窗口数据 + 抽稀。
- 视频从属 EEG 时钟，通过 `sync_to(eeg_time, playing)` 同步。
- 授权：**C++ 原生**（OpenSSL/Botan）RSA-2048 PSS，兼容现有密钥格式。
- i18n：Qt Linguist `.ts/.qm` + QLocale/ICU 数字本地化，按 8 语言（zh_CN/zh_TW/en_US/ja_JP/ko_KR/de_DE/fr_FR/es_ES）设计；边车只回 error_code，UI 层翻译。**不做 RTL**。
- 功能不变以「黄金文件」比对为验收闸门，容差标准逐项记录。
- controller/service 不依赖 UI；UI 不含文件解析与大段 DSP。

## 执行方式

每个里程碑计划顶部标注了 REQUIRED SUB-SKILL。推荐用
`superpowers:subagent-driven-development`（每任务一个新 subagent + 任务间评审）执行；
或用 `superpowers:executing-plans` 在当前会话分批执行并设检查点。
