---
title: M5 回放与标注验收记录
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M5
---

# M5 回放与标注验收记录

M5 在已打开文件会话上提供文件回放控制与事件标注。文件读取仍由 Python 边车负责；Native 负责回放状态、窗口预取、标注列表和波形叠加。

## 范围

- `PlaybackState`：播放、暂停、seek、`0.25/0.5/1/2/4x` 倍速、位置钳制、尾部归零。
- 主窗口回放工具栏：播放/暂停、进度拖动 seek、倍速选择、位置时钟和窗口预取。
- `MarkerStore`：事件导入、按时间插入、颜色轮换、标签编辑、删除和清空。
- `WaveformWidget`：回放光标、顶部通道标注标签、显式标注模式点击新增。
- 边车 `export_markers_csv`：将结构化 markers 写为同名 `_markers.csv` 侧车，并通过 `open` 回读验证。
- i18n：新增回放和标注 UI 文本覆盖 `zh_CN`、`en_US`、`pseudo` 及保留 locale key。

## 协议

`export_markers_csv` 请求参数：

```json
{
  "path": "session.csv",
  "markers": [
    {"time": 0.02, "label": "stim_a"}
  ]
}
```

成功响应：

```json
{"path": "session_markers.csv", "marker_count": 1}
```

错误路径继续使用既有结构化错误：`protocol.invalid_params`、`reader.open_failed`。

## 验证命令

```bash
cmake --build native/build -j$(nproc) --target bf_app m3_dsp_test m4_spectral_test
native/build/m3_dsp_test tests/golden/m3
native/build/m4_spectral_test tests/golden/m4
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m5-playback
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m5-markers tests/fixtures/generated/csv_with_markers.csv
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-ui --open-file tests/fixtures/generated/csv_with_markers.csv
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest \
  tests/native/test_m5_playback_smoke.py \
  tests/sidecar/test_m5_markers_csv.py \
  tests/native/test_m4_spectral_binary.py \
  tests/native/test_m4_analysis_smoke.py \
  tests/native/test_bf_app.py \
  tests/sidecar -q
python3 -m json.tool contracts/error_codes.json >/dev/null
python3 scripts/common/check_i18n.py
git diff --check HEAD
```

## 结果

| 项目 | 结果 |
| --- | --- |
| M3 黄金回归 | PASS |
| M4 黄金回归 | PASS |
| M5 PlaybackState smoke | PASS：五档倍速、非法倍速忽略、尾部归零 |
| M5 MarkerStore smoke | PASS：导入、排序、颜色、编辑、删除 |
| CSV 标注导出回读 | PASS：17 个边车协议测试 |
| Native + sidecar pytest | PASS：91 passed |
| i18n 静态检查 | PASS |
| JSON 与 diff 检查 | PASS |

## 已知边界

- M5 不实现视频同步；视频从属 EEG 时钟、偏移和黑屏逻辑进入 M6。
- M5 不实现片段数据导出、录制或 Session 实时源；这些进入 M7。
- CSV 标注侧车导出已完成；EDF/HDF5 片段导出时写入标注属于 M7。
