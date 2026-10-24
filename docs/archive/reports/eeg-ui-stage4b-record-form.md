---
title: EEG 界面修复阶段 4B：统一录制表单
doc_type: report
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面修复阶段 4B：统一录制表单

## 范围

按 `docs/plans/eeg-ui-recovery.md` 分阶段实施，旧 Python/PySide6 归档只读。由主代理统一集成、构建、版本同步和提交。

## 实现

原 `toggleRecording()` 的动物编号、实验类型、备注、目录四个连续弹窗，替换为一个原生 `RecordMetadataDialog`：统一展示四项输入，提供浏览目录、开始录制和取消。备注支持多行；空动物编号沿用旧版 `unknown` 语义。

默认沿用本次运行中上次选用的目录；首次使用现有 Documents 目录，不存在时使用用户 Home。目录必须非空、存在、为目录且可写；输入时即时校验，点击开始时再次检查。表单本身不创建目录或文件，只有 Accepted 后主窗口才调用现有 RecordController。停止录制和已有后台录制/导出链路保持原实现。

## 验证

- Qt6 configure/build 通过。
- `record_metadata_dialog_test` 覆盖构造与取消无文件副作用、完整元数据和多行 Unicode 备注、目录为空/不存在/是文件/接受前被删除、显式开始一次、四语言重译和空编号回退。
- `--smoke-ui-record-form` 从主窗口录制 QAction 打开统一表单，检查四字段，取消后仍为 `active=0 samples=0`；人工复核中文表单截图。
- `QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-stage4b-layout python3 -m pytest tests/native/test_record_form.py tests/native/test_m7_record_export_smoke.py tests/native/test_fixed_layout.py -q`：**5 passed in 5.47s**，包含表单、主窗口取消、M7 录制 HDF5/EDF、三格式片段导出和四语言布局。
- 版本、i18n、JSON 与差异空白检查通过。

本阶段版本为 `0.1.71-dev`，本地独立提交，未推送、未生成安装包。

## 边界

本轮没有新增录制模式、改写录制协议或替代 RecordController 的最终文件写入错误处理。文件系统状态仍可能在校验后变化，实际启动失败继续由现有错误反馈处理。Windows 桌面与高 DPI 不以离屏测试替代；图外属性和主题属于阶段 5。
