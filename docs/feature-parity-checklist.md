---
title: 当前能力与待验收清单
doc_type: checklist
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# 当前能力与待验收清单

本清单区分已有实现/自动化覆盖和仍待独立验收的事项。勾选不代表所有真实设备、样本和目标桌面均已通过。原逐项 M1–M8 清单与测试细节保存在 [历史清单](archive/reports/feature-parity-checklist-20261020.md) 和 [归档报告](archive/README.md)。本次文档整理不新增功能或验收结论。

## 已有实现与回归

- [x] CSV、HDF5、EDF/BDF、BrainVision、EEGLAB、FIF 浏览路径与单位/切片契约；具体依赖和样本覆盖分别检查。
- [x] 多通道显隐、幅度和时间窗、原始/处理后来源切换、连续播放、预取、拖动预览和请求代次隔离。
- [x] Raw/CAR/Bipolar、陷波及谐波、全局与逐通道带通；DSP 数值回归。
- [x] PSD、FFT、STFT、趋势和相干性；后台调度及图外状态展示。
- [x] 标注编辑、CSV 导入/导出、视频从属时钟同步、偏移、倍速与空白段。
- [x] EDF/HDF5 双格式录制骨架、队列背压、事件写入和丢弃统计。
- [x] 原始/处理后 EDF/HDF5/CSV 片段导出、参数快照、标注时间重基准与回读验证。
- [x] 浏览/分离双工作区、FastICA、人工成分标签/撤销/确认、同步分组预览、固定幅度标尺和结果播放。
- [x] 分离专用 RHD/RHS/HDF5/EDF/CSV 输入、float64 八文件结果、来源指纹、暂存回读和不覆盖发布。
- [x] 非整毫秒分析/导出时间边界回归；输入精度不改变采样率或科学数据精度。
- [x] 四语、主题、区域格式化、pseudo/翻译完整性检查；安装包只含正式语言资源。
- [x] 原生授权实现、固定向量、时间水位和在线激活协议回归；真实许可证兼容另验。
- [x] 冻结边车、Qt/运行库组装、Windows Setup 与 Ubuntu 24.04 x86_64 归档。
- [x] PR 和主线双平台 CI；Windows runner 安装/隔离开发路径运行/卸载，Linux 归档解压后运行。
- [x] 日常 dev 构建身份、阶段版本同步和 Release 预检；错误 tag 拒绝及重跑入口有验证。
- [x] Beta1 annotated tag、独立双平台 Release 构建、GitHub 草稿及六个原始附件核对；实际证据见 [Issue #5](https://github.com/Vsonic12138/boundfree-eeg-native/issues/5)。
- [x] Gitee 分卷准备、原包 SHA256 校验与 Windows/Linux 还原脚本回归；本地准备不代表远端镜像已发布。

## 仍待验收

| 项目 | 要求 / 现有边界 |
| --- | --- |
| 真实格式样本与事件 | Intan RHD/RHS、Open Ephys、Plexon、Blackrock 分别验收连续信号、元数据与 TTL/刺激事件；存在读取入口和合成夹具不替代真实样本 |
| 真实分离效果 | 真实眼周电极记录、采集参考配置、完整 EEGdenoiseNet 半合成评估；不以人工来源标签或参考相关性作为纯净度证明 |
| 新手使用 | 至少三名无 ICA 经验用户完成独立任务走查 |
| Windows 桌面与干净机 | Windows 10/11 安装、覆盖升级、卸载、高 DPI、真实负载和完整交互；runner smoke 不能关闭该项 |
| Linux 桌面 | 实际交付的目标系统和驱动/媒体环境；现有归档不承诺所有发行版兼容 |
| 正式授权兼容 | 正式公钥、旧版真实许可证、在线/离线流程在目标 Windows 安装态验收 |
| 免费核心与扩展授权 | 官方包保留激活，验收发布后允许公开下载、使用仍需有效授权；免费核心免激活、独立扩展及功能权益属于后续阶段，尚待实现与验收 |
| 组织代码签名 | stable 必须 Authenticode 签名且验证通过，不能以 unsigned 包替代 |
| 实际分发依赖与法务 | 已补齐 Qt LGPL/GPL 完整正文并校验完整性；仍须逐包核对 Qt Multimedia/FFmpeg、Python 冻结依赖的许可正文、归属及对应源码获取信息，静态检查不替代二进制分发清单核验 |
| 首轮 beta 公开发布与国内镜像 | GitHub 草稿已经生成；实机、真实授权和分发许可仍待验收。发布后同步 Gitee tag/说明/分卷，并实际访客下载还原核对原包 SHA256；当前尚未公开发布 |
| 历史回归 partial | 标注连接次数与跨不同路径切文件的专用 smoke 仍需按历史收口记录独立核验，不因整理文档补勾 |

BBK/DAT/LSD 目前无正式读取实现；硬件/BLE 真实采集、设备监控和 12 小时长稳未纳入当前交付，后续需另行立项。具体产品范围见 [路线](roadmap.md)。

## 证据使用

- 当前代码与测试是实现依据，旧报告是特定时间/环境的证据，不把其安装包名和计数写成“最新”。
- 数据格式、读片和分离输入的严格边界见 [契约](contracts.md) 与 [分析说明](analysis-guide.md)。
- 云端流程入口及历史验证见 [Issue #2](https://github.com/Vsonic12138/boundfree-eeg-native/issues/2)；每次 beta/stable 另建本仓库发版验收 Issue，记录完整版本、SHA、CI、目标系统、结果与限制。
- 对外发布以 [发布 SOP](release-sop.md) 为准。更新本清单必须附实际证据，不能用归档操作代替验收。
