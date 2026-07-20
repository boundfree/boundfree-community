---
title: M3 DSP 验收记录
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-07-21
---

# M3 DSP 验收记录

## 范围

- 重参考：raw / CAR / bipolar
- 工频陷波（含谐波展开配置）
- 带通（Butterworth 4 阶 SOS）
- 实时 `sosfilt` 状态推进与离线 `sosfiltfilt` 零相位
- 静态浏览接入离线处理
- 黄金文件比对

## 版本

- 产品版本保持 `0.1.0-dev`（未修改 `version.json`）
- IPC 协议版本保持 `1`

## 命令

```bash
python3 scripts/generate_m3_golden.py
cmake -B native/build -S native
cmake --build native/build --target m3_dsp_test bf_app
native/build/m3_dsp_test tests/golden/m3
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-processing-offline
python3 scripts/check_i18n.py
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_bf_app.py -q
```

## 结果

| 项 | 结果 |
| --- | --- |
| m3_dsp_test 黄金比对 | PASS（系数/参考/离线/实时分块） |
| smoke-processing-offline | PASS |
| i18n | PASS |
| tests/native/test_bf_app.py | 33+ passed |

## 容差

见 `tests/golden/m3/meta.json`：

- coeff: rtol 1e-9 / atol 1e-12
- offline: rtol 1e-6 / atol 1e-8
- realtime: rtol 1e-5 / atol 1e-7

## 说明

- DSP 层位于 `native/src/dsp/`，无 Qt 依赖，可独立测试。
- 静态浏览路径：raw slice → offline pipeline → WaveformWidget。
- 单通道独立带通 UI 未完整铺开，当前提供全局带通开关；配置结构已支持每通道扩展。
