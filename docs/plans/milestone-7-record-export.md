# Milestone 7：录制与导出 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M3、M5 完成后定稿。

**Goal:** 经 Python 边车实现双格式（EDF+HDF5）边采边存录制（含背压与事件追加）与片段导出（EDF/HDF5/CSV，含标注写入）。

**Architecture:** 主程序实时管线把原始 chunk 经 IPC 批量通道交给边车编码；边车复用 pyedflib/h5py/pandas（旧版 recorder/export_service 逻辑）。录制/导出为可取消后台任务，UI 只显示进度/结果/错误。

**Tech Stack:** C++/Qt6、IPC、边车（pyedflib/h5py/pandas）。

## Dependencies
- M3（实时管线/DSP）、M5（回放/会话，导出取当前 reader 片段）。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/controller/record_controller.{h,cpp}` | 录制会话、chunk 送边车、背压状态 |
| `native/controller/export_controller.{h,cpp}` | 导出请求校验、异步导出协调 |
| `native/ui/record_panel.{h,cpp}` | 开始/停止、REC 闪烁、计时、磁盘监控、元数据对话框 |
| `sidecar/boundfree_sidecar/codecs/recorder.py` | 双格式流式写（复用旧版 recorder.py） |
| `sidecar/boundfree_sidecar/codecs/export.py` | 片段导出 + 标注写入（复用旧版 export_service.py） |
| `tests/sidecar/`, `tests/native/` | 背压、往返、异步测试 |

## Tasks

### Task 1：边车录制（双格式 + 背压 + 事件）
- **Interfaces Produces:** IPC `record_open{fs,ch_names,out_dir,meta,formats}`→`{handle}`、`record_write{handle,chunk}`→`{ok,backlog,dropped}`、`record_event{handle,ts,label}`、`record_close{handle}`→`{paths}`。
- **Tests（TDD，pytest）:** 有界队列满时返回 dropped 计数（等价旧版 backpressure）；EDF+HDF5 同时产出；事件写入 HDF5 events；HDF5 maxshape 可扩展、float32。
- **DoD:** 与旧版 recorder 行为一致；写线程单一句柄（无竞态）。

### Task 2：录制控制器与面板
- **Deliverable:** 元数据对话框（animal_id/exp_type/notes/dir）、REC 闪烁、耗时计时、磁盘剩余监控（<500MB 警告）、队列拥堵状态提示。
- **Tests:** offscreen 冒烟：开始/停止流转；无 EEG 时提示。
- **DoD:** 行为与旧版 record_panel 一致。

### Task 3：片段导出（异步 + 标注）
- **Interfaces Produces:** IPC `export_segment{src,t0,t1,path,format,markers}`→`{ok,marker_count}`/`{error_code}`；`ExportController::exportAsync(req)`、信号 `done/failed`。
- **Tests（TDD）:** EDF→Annotations、HDF5→events/time+label、CSV→侧车 `_markers.csv`；无 reader/非法区间返回 error_code；导出中 UI 显忙碌。
- **DoD:** 导出文件可被边车再打开、标注往返一致。

### Task 4：导出格式往返黄金比对
- **Tests:** 用旧版导出同输入产出为基准，新版导出结果元数据等价、数据在容差内一致（EDF 允许量化误差，HDF5/CSV 数值一致）。
- **DoD:** 往返一致性通过。

## Acceptance Criteria
- 双格式录制、背压、事件追加与旧版一致。
- 片段导出三格式含标注，异步不阻塞 UI。
- 导出往返通过黄金比对。

## Parity Gate
- 勾验清单：双格式录制、背压、磁盘监控、事件追加、片段导出（EDF/HDF5/CSV）、导出标注。
