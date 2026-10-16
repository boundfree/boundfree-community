---
title: EEG 阶段 1A 频谱曲线修复验收
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 阶段 1A 频谱曲线修复验收

## 结果与范围

SpectrumWidget 已生成路径却未调用绘制，导致 PSD、FFT、相干性只有坐标/背景而没有谱线。补回 drawPath，不改算法、数据与布局。新建 spectrum_render_test，并通过 pytest 包装接入既有测试方式。

## 验证证据

环境：WSL2 Linux、仓库 Qt6 conda 工具链、Qt offscreen。CMake configure 与完整 build 成功。

- 修复前构建新测试：12 组合曲线像素差均为 0，退出码 1。
- 修复后：PSD/FFT/相干 × 亮暗 × 频带开关，12 组合均通过。
- 同一个控件先画单点 move-only path，再画多点谱线；比较绘图区内部差异，避免把频段背景、标题或占位文字算成曲线。
- `QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_spectrum_render.py tests/native/test_m4_analysis_smoke.py -q`：3 passed。

执行构建：在 `native/` 下运行 `cmake --preset wsl2-qt6-conda`、`cmake --build --preset wsl2-qt6-conda -j 4`。

## 限制

这是共用曲线绘制缺陷修复，不代表用户报告的全部四页分析问题已经闭环。时频/趋势数据范围、单点趋势、主窗口重复调度与错误反馈仍列入后续分析验收。原 smoke 的非背景像素计数不足以证明曲线存在，新增测试承担此回归门禁。未执行 Windows 桌面视觉验收，也未生成或发布安装包。
