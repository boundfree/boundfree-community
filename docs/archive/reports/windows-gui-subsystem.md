---
title: Windows 桌面启动不再显示终端
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# Windows 桌面启动不再显示终端

桌面版 `bf_app.exe` 之前按 Windows 控制台子系统链接。用户从 Explorer、开始菜单或桌面快捷方式启动时，Windows 因此打开控制台窗口；Qt Multimedia 的 FFmpeg 初始化信息显示在这个窗口里。

Windows 构建将 `bf_app` 的 `WIN32_EXECUTABLE` 设为 true，让 CMake/Qt 用 GUI 子系统链接，同时保留现有 `main()` 与参数分发。正常桌面启动不再创建附属终端；`--version` 和仓库自动验收使用的 `--smoke-*` 参数仍由同一个可执行程序处理。

## 验证

使用 Windows 11/MSVC Release 构建并完成 Windows 暂存部署。检查 PE 可选头的 subsystem 为 `IMAGE_SUBSYSTEM_WINDOWS_GUI (2)`；运行 Windows 自动验收，覆盖版本输出、系统 UI、边车握手、授权门禁、CSV/EDF 读取、离线处理、回放、频谱、录制与导出。

这验证了链接类型与命令行验收兼容性。真实用户会话中通过 Explorer/快捷方式启动后 FFmpeg 信息不再伴随控制台窗口，需在安装新版后做最终桌面确认。
