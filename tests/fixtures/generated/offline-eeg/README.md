---
title: 离线脑电眼电分离受控 Intan 合成测试文件
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-10-20
---

# 离线脑电眼电分离受控 Intan 合成测试文件

这两个文件由 BoundFree 自有的 Pro 离线分析回归测试生成，并复制进 Community 测试资源，供独立检出仓库和 CI 验证 RHD/RHS 读取、通道映射、抗混叠、分离和导出。它们不是人体/患者数据、设备采集、Intan 官方样本或 EEGdenoiseNet 数据，不包含外部供应商数据资产。

来源生成器为 Pro 的 `tests/eeg-analysis-regression.cpp` 中 `fixture()`，工作区原始路径为 `test-data/boundfree-pro/offline-eeg-synthetic/`。原始生成入口是在 Pro 仓库运行 `python3 scripts/run-eeg-regression.py --build-dir ../build/intan-rhx --examples-dir ../test-data/boundfree-pro/offline-eeg-synthetic`；该入口仅说明来源，Community 测试直接使用本目录的固定文件，不依赖 Pro 源码或工作区外路径。

文件均为 2000 Hz、24576 点（12.288 秒）的受控数学混合。RHD/RHS 的 AC 通道数据相同；RHS 额外有 DC / 刺激数据布局测试区域，采样率是软件布局测试参数，不代表 RHS 硬件选项。文件头另含一个未启用通道。

| 启用通道 | 测试用途 |
| --- | --- |
| A-000、A-002、A-003 | 三路 EEG/EOG/EMG 数学混合；A-000 另含 800 Hz 抗混叠测试分量 |
| A-004 | 纯合成慢波与眨眼参考 |
| A-005 | 纯合成突发噪声参考 |
| A-006 | 平线，必须排除；用于异常输入拒绝测试 |

SHA-256：

```text
383b840845170429f5d2b75745f65fa706cfcf63043e5f3247393296d9eff247  synthetic.rhd
51618203918d17f4a33b82a2cc822510d5948220c9da5b766f253f5ea5cabac0  synthetic.rhs
```

合成纯参考和数学线性混合只验证受控条件，不保证真实电极记录中脑电、眼电、肌电可被准确分离。
