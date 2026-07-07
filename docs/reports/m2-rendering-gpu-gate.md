---
title: M2 真实 GPU 渲染门禁记录
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-07-08
---

# M2 真实 GPU 渲染门禁记录

M2 波形浏览验收前必须在真实 Windows 或 Linux GPU/显示环境复测。WSL、Xvfb、Mesa 软件渲染只作为开发信号，不作为最终验收。

## Required Scenario

- Channels: 64, 128, 256.
- Sampling rate: 1 kHz.
- Visible window: 4 s.
- Interactions: scroll, zoom, channel visibility toggle.
- Required record: OS, GPU, driver, Qt version, command, FPS, CPU usage, GPU usage, subjective interaction notes.

## Acceptance

- Preferred: 256 channels at 1 kHz, 4 s visible window, at least 55 FPS.
- Acceptable with note: at least 30 FPS and interactions remain smooth.
- Blocked: below 30 FPS or visible interaction stalls.
