---
title: M7 录制与片段导出验收记录
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-07-22
milestone: M7
---

# M7 录制与片段导出验收记录

## 结论

M7 文件回放录制骨架、双层有界队列背压、EDF+HDF5 双格式写入、HDF5 事件追加以及 EDF/HDF5/CSV 异步片段导出已实现并通过自动化与端到端 smoke。下一阶段为 M8 Windows 打包与发布验收。

本结论只覆盖文件回放源，不代表 BLE、真实设备录制或 12 小时采集长稳已经完成。

## 实现范围

- stdio clean cutover 为 `u8 frame_type + u32le payload_length + payload`；JSON 和二进制 payload 统一分帧。
- `get_slice` 使用 JSON 元数据帧加 `float32-le` EEG 数据帧，不再传输 JSON 浮点二维数组。
- `record_open/record_write/record_event/record_close`：双格式录制、边车有界队列、排空关闭和结构化统计。
- Native 文件回放源与边车写入器使用独立进程；Native 有界队列与边车写队列分别统计丢弃。
- 文件源按单调时钟和固定 chunk 顺序产生数据，不复用 UI 预取窗口。
- HDF5 `eeg` 使用 `float32`、可扩展 `maxshape` 和约一秒 chunk；事件为 `events/time` 与 `events/label`，由同一写线程追加。
- `export_segment` 支持 EDF/HDF5/CSV；标注按区间筛选并重基准到片段起点。
- EDF 按标注数量配置 annotation signals，覆盖同一短片段的多个标注。
- CSV 输出包含 `time_s`；HDF5 reader 补齐事件回读，三格式均可通过边车重新打开。
- 主窗口浏览工具栏接入录制与片段导出；录制显示耗时、样本、丢弃和低于 500 MiB 磁盘告警。

## 依赖与环境

- 环境：WSL2 Linux，Python 3.10，Qt 6.9.2，offscreen。
- 写入依赖：`h5py==3.16.0`、`pyedflib==0.1.42`。
- 读取与 EDF 往返：`mne==1.12.1`。
- 产品版本保持 `0.1.0-dev`，IPC 协议版本保持 `1`。

## 端到端结果

```text
┌──────────────────────┬──────────────────────────────────────────────────┐
│ 场景                 │ 观察结果                                         │
├──────────────────────┼──────────────────────────────────────────────────┤
│ 双格式录制 smoke     │ 10 samples，source dropped=0，writer dropped=0  │
│ HDF5 录制事件        │ 1 个事件重新打开后可见                           │
│ 三格式导出 smoke     │ EDF/HDF5/CSV 均重新打开成功                      │
│ 导出异步事件循环     │ 最终验证期间 Qt 事件循环 300 次 tick             │
│ M7 sidecar 专项      │ 72 passed                                        │
│ Native + sidecar     │ 166 passed                                       │
│ 全量 Python 测试     │ 192 passed                                       │
│ M3/M4 黄金二进制     │ PASS                                             │
│ i18n / 错误码契约    │ PASS                                             │
└──────────────────────┴──────────────────────────────────────────────────┘
```

三格式导出 smoke 修复 EDF 多标注容量后重复执行均通过；最终结果为 `formats=3`，Qt 事件循环 300 次 tick。

## 验证命令

```text
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda -j
native/build/bf_app --smoke-m7-record tests/fixtures/generated/simple_eeg.csv /tmp/bf-m7-record
native/build/bf_app --smoke-m7-export tests/fixtures/generated/simple_eeg.csv /tmp/bf-m7-export
PYTHONPATH=sidecar python3 -m pytest tests/sidecar/test_m7_framing.py tests/sidecar/test_m7_recording.py tests/sidecar/test_m7_export.py -q
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native tests/sidecar -q
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q
native/build/m3_dsp_test tests/golden/m3
native/build/m4_spectral_test tests/golden/m4
python3 scripts/check_i18n.py
python3 -m json.tool contracts/error_codes.json
```

CMake 命令从 `native/` 目录执行，其余命令从仓库根目录执行。

## 数值与往返口径

- HDF5 与 CSV 按导出 `float32` 精度比较数据语义。
- EDF 允许由 `physical_min=-3200`、`physical_max=3200` 与 16 位数字范围产生的量化误差，不要求逐字节一致。
- 自动化测试比较采样率、通道、逻辑样本数、数据、单位和标注；EDF 子秒数据记录允许在磁盘中按格式规则填充到整记录。
- 本阶段没有提交临时录制输出或客户样本。

## 已知边界

- 当前录制源是文件回放源；未接入 BLE、自研硬件或真实设备状态。
- 本次验证使用 WSL2 offscreen；REC 闪烁、对话框布局和真实 Windows 磁盘告警视觉效果需在 M8 Windows 发布验收复核。
- 录制不支持暂停/续录、跨文件滚动、多设备或 12 小时长稳。
- PyInstaller、Inno Setup、代码签名及发布版依赖许可证材料属于 M8。
