---
title: 离线分离教学记录
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-20
---

# 离线分离教学记录

`teaching.h5` 是 BoundFree 自行生成的数学信号：振荡、慢变化/眨眼形状与突发噪声三源，混合为三路观测，另两路提供眼/肌参考。它不包含病人、动物或外部数据库记录，不代表真实生理分离效果。

`manifest.json` 固定文件 SHA-256、采样信息、通道用途、混合矩阵和生成种子。仅文件指纹和元数据完全匹配时自动预填角色；示例标记额外限定完整 16 秒、500 Hz、无陷波、既定通道用途和三分量，不能套用到真实记录。

从仓库根运行 `python scripts/common/generate_separation_teaching.py` 可重新生成并更新清单；需要 NumPy 和 h5py。生成后的文件是可分发教学资产，随 CMake install 和 Windows 安装包进入 `samples/offline-eeg/`。
