---
title: 产品范围与当前路线
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-10-24
---

# 产品范围与当前路线

Community 主线面向 EEG/LFP 文件浏览与科研分析，使用 C++/Qt6 主程序和 Python 编解码边车。产品版本与交付阶段以根目录 [version.json](../version.json) 为准；当前为 beta 候选阶段，Beta1 独立构建与 GitHub 草稿已经生成，尚未公开发布，未完成 stable 发布验收。

## 已有能力

- 多通道文件浏览、原始/处理后来源切换、重参考、陷波、全局及逐通道带通。
- PSD、FFT、STFT、频段趋势、相干性、连续播放、标注、视频同步、双格式录制骨架和 EDF/HDF5/CSV 片段导出。
- 专用脑电/眼电离线分离工作区：独立输入、FastICA、人工成分检查与撤销、同步分组预览、结果播放和可追溯 float64 导出。
- 四语界面、原生授权校验、冻结边车及 Windows/Linux 云端构建。主线更新生成内部安装包，正式 Release 由人工选择版本并检查草稿。

上述为已实现和已有自动化覆盖的能力，不等于所有真实科研样本、目标机器或电极效果都已验收。具体边界见 [验收清单](feature-parity-checklist.md) 和 [分析说明](analysis-guide.md)。

## 当前优先事项

| 工作 | 需要取得的证据 |
| --- | --- |
| 真实记录与算法评估 | 眼周记录、明确电极/参考配置、完整 EEGdenoiseNet 半合成基准；独立评估分离效果 |
| 新手操作走查 | 至少三名无 ICA 经验用户完成导入、通道确认、成分检查与导出 |
| 桌面与干净机验证 | Windows 10/11 干净机、升级/卸载、高 DPI、真实授权兼容；交付 Linux 时验证目标桌面 |
| 首轮 beta 发布验收 | 已创建 Beta1 tag 并跑通独立构建/草稿，见 [Issue #5](https://github.com/Vsonic12138/boundfree-eeg-native/issues/5)；继续完成实机、真实授权与分发许可核验，发布后验证 Gitee 国内镜像 |
| stable 交付 | 完成本次承诺范围的验收与 Windows 组织代码签名，不绕过发布门禁 |

CI 已在 Windows runner 验证安装、隔离开发搜索路径后运行与卸载，在 Ubuntu 24.04 验证归档解压后运行；这些证据不替代上表人工验收。版本、构建与发布操作统一见 [发布 SOP](release-sop.md)。

## 范围边界

- Community 当前交付聚焦 EEG/LFP 与眼电/肌电混合信号的离线分析；Pro 是独立的 Spike 采集与观测产品，两者不互相迁入设备控制和商业代码。
- 本仓库仍保留 `bf_spike` 底座原型及其测试，属于历史探索，不作为 Community 当前发布承诺。本次文档收口不改变代码目标。
- BLE、自研硬件、真实设备采集闭环与长期采集验收尚未纳入当前交付；后续须另行确定范围，不沿用旧规划中的时间估计或阶段承诺。
- 供应商格式需区分“有读取入口”“合成样本回归”“真实样本验收”与“事件支持”；不能互相替代。BBK/DAT/LSD 当前没有正式读取实现。

## 历史依据

M0–M8、UI 修复、早期产品分线与设计草案均在 [归档索引](archive/README.md)。历史 GPU 基线保留在 [M2 记录](archive/reports/m2-rendering-gpu-gate.md)，不能自动代表当前所有桌面负载的性能结论。云端流程验收见 [Issue #2](https://github.com/Vsonic12138/boundfree-eeg-native/issues/2)，具体发版仍需独立验收 Issue。
