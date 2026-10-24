---
title: 当前架构与模块边界
doc_type: architecture
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# 当前架构与模块边界

本文描述当前源码结构；产品范围见 [路线](roadmap.md)，协议细节以 [基础契约](contracts.md) 为准。最初的迁移设计及其未采用选项保存在 [原设计快照](archive/design/architecture-design-20261020.md)。

## 运行结构

`bf_app` 是 C++17 / Qt6 Widgets 主程序，负责界面、控制、数值计算、授权与国际化；独立 Python 边车负责科研文件的解码、编码与来源校验。主程序通过子进程 stdio 交换有长度前缀的 JSON 与二进制帧。

| 模块 | 源码位置 | 职责 |
| --- | --- | --- |
| 应用入口 | `native/src/app/` | 启动、资源定位、授权门禁与 smoke 入口 |
| UI | `native/src/ui/` | 浏览/分离双工作区、波形和分析绘图、参数与状态展示 |
| 控制器 | `native/src/controller/` | 文件会话、读片代次、播放、分析、录制和导出协调 |
| DSP | `native/src/dsp/` | 重参考、SOS 滤波、频谱、自有白化与 FastICA、分组重建 |
| 数据与绘制辅助 | `native/src/render/` | 波形切片、抽稀与显示模型 |
| IPC | `native/src/ipc/` | 边车启动、握手、分帧、超时及二进制数组传输 |
| 授权 / i18n | `native/src/license/`、`native/src/i18n/` | 原生许可证校验和在线激活客户端、翻译与区域格式化 |
| 文件 I/O | `sidecar/boundfree_sidecar/` | 浏览读取器、专用 `analysis_io/`、录制/导出与错误码 |

CMake 目标中，`bf_dsp` 是数值静态库，`bf_core` 汇集可复用控制/IPC/授权/绘制基础，`bf_app` 承载 EEG 主界面；`bf_spike` 是保留的历史原型。实际文件列表以 `native/CMakeLists.txt` 为准。

依赖保持从 UI 到控制层再到数值与 I/O；DSP 不依赖界面，UI 不解析科研文件。当前波形控件是 `QWidget` 绘制路径，不能把早期 `QOpenGLWidget` 方案当成现有实现。频谱已经使用随仓库提供的 pocketfft，滤波和 ICA 为自有 C++ 实现，不能再将这些模块列为待引入。

## 浏览、分析与导出

浏览使用 `FileOpenController` 管理会话和最新请求，切片缓存与可见时间窗分离。原始/处理后模式同时决定波形和频谱输入；处理模式使用离线零相位滤波。播放按需预取，分析在后台执行并丢弃过期响应，数据不足时不继续推进时间。

片段导出捕获参数与缓存上下文，原始导出交给边车读取/编码；处理后导出由 Native 处理样本，再以二进制 IPC 交给边车编码。标注重基准到片段起点。常规浏览与片段导出使用 float32 帧，不能与专用分离的 float64 契约混用。

录制为文件回放/模拟来源的写盘链路，使用有界队列、背压和丢弃计数；不据此宣称真实设备采集已完成。视频从属 EEG 播放时钟。

## 专用离线分离

`EegSeparationWorkspace` 与浏览工作区使用外层栈切换；切换暂停对应播放并保留内存状态，不提供跨重启项目恢复。`EegSeparationController` 持有独立来源快照、分析设置、任务代次、人工标签、撤销和确认，不继承浏览滤波。

专用输入/输出使用能力协商后的 `float64-le`。边车验证文件、时间轴、单位和指纹；Native 负责抗混叠重采样、可选陷波、拟合副本高通、白化/FastICA 与未做该高通的输出副本重建。相关数值来源的历史对照见 [归档证据](archive/reports/offline-eeg-numerical-provenance.md)。

读入和计算采用协作取消与任务代次隔离；人工标签只更新缓存贡献，不重新拟合。导出先暂存、逐值回读、验证单次均值重建和源指纹，再以不覆盖语义发布新目录；发布临界区短暂禁止取消。结果播放仅改变游标和视窗，不修改科学数组或确认。

## 文件、资源与授权

- 浏览读取与专用分析读取是两套能力入口。专用五种格式条件见 [分析说明](analysis-guide.md)，精确 schema 和帧格式见契约；读取器注册不等于真实供应商样本及事件已验收。
- 发布包含冻结边车、Qt 运行时、`.qm`、版本、错误码、法务与教学资源；开发态 Python 回退不能代替安装态验证。
- 界面已提供 `zh_CN/en_US/ja_JP/zh_TW`，pseudo 仅用于开发测试。后端只返回 `code + params`，UI 统一映射文案。
- 授权留在 Native，Windows 使用 CNG，Linux 使用 OpenSSL；机器标识采用旧版兼容的 MachineGuid SHA-256 前 32 位大写十六进制，MAC 不参与。
- `license.dat` 与时间水位 `.last_run` 的校验/原子保存、在线恢复逻辑由授权模块维护。安装包保留 `BF_REQUIRE_LICENSE=ON`；授权签发私钥不进入客户端、仓库或构建环境。
- 服务端独立维护；[旧在线激活设计](archive/design/online-activation-design.md) 仅供背景追溯，不能直接作为部署指令。当前客户端行为以 `native/src/license/` 和相应回归为准。

## 开源核心与扩展边界

本仓库自有实现（含当前 ICA）采用 Apache-2.0，第三方许可不变。未来商业扩展在独立私有仓库维护源码、商业许可、构建与产物，核心只提供公开且版本化的集成接口，并须能独立构建运行。当前没有已实现的商业插件接口或按功能授权系统；现有整机激活的迁移仍待独立实施。规则与贡献范围统一见 [许可与扩展边界](../legal/LICENSING.md)。

## 开发与交付入口

本地环境和构建入口见 [工具链](toolchain-wsl2.md)，提交与 PR 见 [交付 SOP](contribution-sop.md)，双平台 CI、版本和 Release 见 [发布 SOP](release-sop.md)。当前未验收项统一维护在 [验收清单](feature-parity-checklist.md)，不在架构文档重复维护测试计数与安装包名称。
