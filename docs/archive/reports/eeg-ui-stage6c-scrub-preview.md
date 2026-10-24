---
title: EEG 界面阶段 6C 拖动波形预览
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面阶段 6C 拖动波形预览

进度条拖动期间，缓存覆盖目标时立即移动波形视口；缓存外以 80ms 单次定时器合并目标、最多保持一次在途预览读取。返回数据只有覆盖当前目标才显示，否则保留上一幅波形并继续读取最新目标。复用 FileOpenController 的持久 worker、latest mailbox 与 generation，不改变 IPC 契约。

拖动时更新目标时间，暂停回放推进和视频播放，取消旧分析且不触发新分析；松手后进行一次最终准确定位，恢复既有播放/分析流程。旧预览在新定位/换文件后由控制器 generation 淘汰。键盘和轨道步进保持直接定位。

取消拖动（如读取失败或打开另一文件）时，提交最后成功显示的预览位置，保证数字、滑条、游标和波形一致；若尚未显示新预览则保留原位置。不保存另一整块缓存用于拖前恢复。独立审查发现并修复了“成功预览后下一次失败，数字回原处而波形仍停预览位置”的漏洞，并补充失败回归。

## 验证

WSL2 Qt6 configure/full build 通过。最终联合命令：

```bash
QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-stage6-final python3 -m pytest tests/native/test_empty_controls.py tests/native/test_fixed_layout.py tests/native/test_channel_parameters.py tests/native/test_seek_preview_smoke.py tests/native/test_scrub_preview.py tests/native/test_continuous_file.py tests/native/test_continuous_playback.py tests/native/test_bf_app.py tests/native/test_analysis_presentation.py tests/native/test_spectrum_render.py tests/native/test_waveform_channels.py tests/native/test_waveform_viewport.py tests/native/test_record_form.py tests/native/test_m4_analysis_smoke.py tests/native/test_m5_playback_smoke.py tests/native/test_m6_video_sync.py -q
```

结果 **69 passed in 33.68s**。覆盖第二轮空态、箭头和常用值预设、参数页，以及现有分析、连续回放、录制表单、视频同步回归。

新增真实 CSV → Python sidecar → FileOpenController → MainWindow 集成用例：ramp 信号检查实际切片样本；按住滑条成功显示 60 秒；快速目标合并到 70 秒并在真实读取在途改为 75 秒，70 秒返回不得覆盖当前波形，随后显示 75 秒；松手最终准确提交 76.25 秒。验证总窗口请求严格为 4 次，阶段由读取事件推进，15 秒总超时，不假设机器短时 IO 延迟。模拟回归另覆盖缓存即时预览、timer 不覆盖、分析暂停、错误取消及新文件打开失败后一致性。

## 边界

缓存外预览仍需等待实际读片与滤波；80ms 是调度间隔，不是端到端延迟保证。离线滤波仍在 UI 线程，大文件、高采样率、多通道流畅度与真实 Windows/DPI 门禁未验收。保留原连续前向视窗语义；不宣称旧版尾随历史播放方式已实现。未修改用户既有授权 smoke 和 Windows 打包改动，未推送远端或发布安装包。
