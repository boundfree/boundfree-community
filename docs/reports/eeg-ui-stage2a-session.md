---
title: EEG 阶段 2A 读片会话复用验收
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 阶段 2A 读片会话复用验收

## 实现

FileOpenController 改为单个持久工作线程，线程内独占 SidecarClient/QProcess。同文件 openWindow 复用 handle；换文件先 close 再 open；显式 openInitialWindow 通过 revision 强制重新打开同路径。单 pending 槽合并尚未执行请求，generation 阻止旧结果回调。失败时清理会话，下个请求重建连接；错误先按值保存，避免清理 opened 时清空其 error 引用。

## 验证

环境：WSL2 Linux、仓库 Qt6 conda 工具链、offscreen。CMake configure 与完整 build 通过。

- 真实 SidecarClient 帧协议配合测试用计数边车，顺序三个同文件切片仅一次 hello/open。
- 同路径显式重开与换路径均关闭旧 handle；请求日志确认重开。
- 在途 10 秒请求受控阻塞期间依次请求 11/12/13，日志只读取 10 与 13，仅交付 13。
- 打开失败、进程崩溃后重新连接成功；错误码分别保留 reader.file_not_found 与 ipc.*。
- 在途销毁丢弃 pending 与回调，并在工作线程关闭 handle 与进程。
- `QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_file_session_reuse.py tests/native/test_bf_app.py tests/native/test_waveform_viewport.py -q`：42 passed（最后一项为后续视窗控件的先行验证，不属于本提交）。加强错误码断言后再次运行会话测试：1 passed。
- 真实文件不存在回归曾暴露清理时错误码丢失，修复后通过；原有实际文件打开、切片、latest-window 和销毁回归通过。

## 边界

确认的是进程/句柄复用与请求数量变化，不是 Windows 实机耗时或帧率结论。正在进行的同步 IPC 仍不能立即取消，析构等待受读取、close 与 stop 超时影响。未改波形显示；连续视窗在阶段 2B 独立提交。未涉及协议格式、许可证或打包。
