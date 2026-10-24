---
title: M2 真实 GPU 渲染门禁记录
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-07-21
archived_at: 2026-10-23
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

## Benchmark Command

Development smoke command:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 30
```

Machine-readable development smoke command:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 30 --json
```

This command is a development signal only when run offscreen, under WSL, Xvfb, or Mesa software rendering. It does not close the real GPU/display gate.

Real GPU gate command template:

```bash
native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 120
```

Real GPU result rows must include:

- OS and display environment.
- GPU model and driver version.
- Qt version and command.
- FPS for 64, 128, and 256 channels.
- CPU and GPU utilization source.
- Scroll, zoom, and channel-visibility subjective notes.

## Result Log

| Date | Environment | Channels | fs | Window | FPS | CPU | GPU | Interaction Notes | Gate |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-07-21 | Windows 10 Pro 19045 desktop / NVIDIA GeForce RTX 4070 SUPER / driver 32.0.15.1062 / Qt 6.8.2 | 64 | 1000 Hz | 4 s | 112.99 | Task Manager while benchmark | Task Manager GPU 3D while benchmark | scroll/zoom/channel visibility: smooth | Passed |
| 2026-07-21 | Windows 10 Pro 19045 desktop / NVIDIA GeForce RTX 4070 SUPER / driver 32.0.15.1062 / Qt 6.8.2 | 128 | 1000 Hz | 4 s | 110.50 | Task Manager while benchmark | Task Manager GPU 3D while benchmark | scroll/zoom/channel visibility: smooth | Passed |
| 2026-07-21 | Windows 10 Pro 19045 desktop / NVIDIA GeForce RTX 4070 SUPER / driver 32.0.15.1062 / Qt 6.8.2 | 256 | 1000 Hz | 4 s | 111.32 | Task Manager while benchmark | Task Manager GPU 3D while benchmark | scroll/zoom/channel visibility: smooth | Passed |

### 2026-07-21 real-windows-gpu

- Gate: Passed
- OS/display: Windows 10 Pro 19045 desktop
- GPU: NVIDIA GeForce RTX 4070 SUPER, driver 32.0.15.1062
- Qt: 6.8.2
- CPU: Task Manager while benchmark
- GPU usage: Task Manager GPU 3D while benchmark

| Channels | fs | Window | Frames | FPS |
| --- | --- | --- | --- | --- |
| 64 | 1000 | 4 | 120 | 112.99 |
| 128 | 1000 | 4 | 120 | 110.50 |
| 256 | 1000 | 4 | 120 | 111.32 |

Interaction notes:

- Scroll: smooth
- Zoom: smooth
- Channel visibility: smooth

Commands:

- `native\build-windows\Release\bf_app.exe --smoke-waveform-benchmark --channels 64 --fs 1000 --seconds 4 --frames 120 --json`
- `native\build-windows\Release\bf_app.exe --smoke-waveform-benchmark --channels 128 --fs 1000 --seconds 4 --frames 120 --json`
- `native\build-windows\Release\bf_app.exe --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 120 --json`

Acceptance run used `scripts/common/collect_m2_gpu_gate_evidence.py --run --acceptance-run` with `development_reasons: []` and `gate: Passed`.

## M2-3 Status

M2-3 adds machine-readable benchmark output and browser interaction smokes. These are still development evidence unless run on a real GPU/display environment.

## M2-4 Runbook

M2-4 uses `scripts/common/collect_m2_gpu_gate_evidence.py` to run or format the 64/128/256 channel benchmark set.

Development dry-run:

```bash
python3 scripts/common/collect_m2_gpu_gate_evidence.py --dry-run
```

Real GPU/display run template:

```bash
python3 scripts/common/collect_m2_gpu_gate_evidence.py --run --acceptance-run \
  --environment-label "real-windows-gpu" \
  --os-display "Windows 11 desktop" \
  --gpu-model "record GPU model" \
  --driver-version "record driver version" \
  --qt-version "record Qt version" \
  --cpu-observation "record CPU usage and source" \
  --gpu-observation "record GPU usage and source" \
  --scroll-note "record scroll interaction" \
  --zoom-note "record zoom interaction" \
  --channel-visibility-note "record channel visibility interaction"
```

The script runs these required scenarios:

- 64 channels, 1 kHz, 4 s window, 120 frames.
- 128 channels, 1 kHz, 4 s window, 120 frames.
- 256 channels, 1 kHz, 4 s window, 120 frames.

Development/offscreen results must not be copied as acceptance evidence. If the run happens under WSL, Xvfb, Mesa software rendering, or `QT_QPA_PLATFORM=offscreen`, the gate remains `Open`.

Current gate status: Passed.
