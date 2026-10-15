# M0 Spike Report

Date: 2026-07-05

## Summary

M0 built a minimal C++/Qt6 spike, a QOpenGLWidget waveform renderer, a Python
stdio sidecar, and a C++ IPC benchmark.

Result:

- IPC: **go** for M1's initial stdio + length-prefixed binary protocol.
- Rendering: **conditional**. The code builds and runs under Xvfb software GL, but
  this WSL session has no real display/GPU context, so the 256ch hardware FPS gate
  is not yet satisfied.
- Python baseline: confirms high MNE open latency and DSP/filtering hotspots.

## Spike-1 Rendering

Environment:

- WSL2 Ubuntu 22.04, no `DISPLAY`.
- `QT_QPA_PLATFORM=offscreen` does not support `QOpenGLWidget`.
- Xvfb + Mesa software rendering was used only to verify the OpenGL path.

Software-rendered results:

| Channels | Window | Result |
| --- | ---: | --- |
| 64 | 4 s @ 1000 Hz | 30-47 FPS |
| 128 | 4 s @ 1000 Hz | 27 FPS |
| 256 | 4 s @ 1000 Hz | 19-21 FPS |

Decision:

- These numbers are not a valid GPU acceptance result.
- Before M1, rerun `bf_spike` on a Windows/Linux machine with a real GPU/display.
- Target remains 256ch @ 1 kHz, 4 s visible window, >=55 FPS preferred or >=30 FPS
  with smooth interaction.

## Spike-2 IPC

Real EDF sample:

- File: `../boundfree-eeg-upper-computer/samples/mark.edf`
- Metadata: 1024 Hz, 3 channels, 1492 s, 2 markers.
- `open`: 18728 ms, including Python/MNE cold start and file open.
- `get_slice(1s)`: 0.26 ms average over 100 iterations.

Synthetic bulk payload:

- Source: `synthetic://256x1000x200`
- Payload: 256 channels x 1000 samples x float32 = 0.977 MiB.
- `open`: 133-3136 ms observed, depending on process cold-start/cache state.
- `get_slice(1s)`: 2.94-4.82 ms average over 100 iterations.
- Effective throughput: 202.61-332.16 MiB/s.

Decision:

- Stdio + JSON control + length-prefixed binary payload is sufficient for M1/M2.
- Shared memory is not required initially.
- M1 should still formalize little-endian framing, async reads, request IDs,
  timeout policy, and sidecar restart behavior.

## Python Baseline

Existing reference tests:

- `tests/test_playback_performance.py tests/test_waveform_performance.py`
- Result: `10 passed in 23.98s`

DSP profile:

| Path | Workload | Average |
| --- | --- | ---: |
| Offline DSP | 10 s, 256 ch | 434.12 ms |
| Realtime DSP | 0.5 s chunk, 256 ch | 14.51 ms |

File IO:

- `read_file_lazy(samples/mark.edf)` open: 42574.27 ms.
- 100x `get_slice(1s)` average: 0.118 ms.

Interpretation:

- MNE open must be asynchronous with UI busy/progress state.
- DSP migration should cache filter coefficients and avoid repeated per-channel
  SciPy design overhead.
- Python rendering coverage is only proven for 64ch budget tests, not 256ch FPS.

## Go / No-Go

Decision: **conditional go**.

Proceed to M1 design/detailing for:

- CMake/Qt6 project skeleton.
- Python sidecar lifecycle.
- stdio/binary IPC protocol.
- authorization/i18n skeleton.

Do not treat rendering as fully validated until one hardware GPU run records:

- 64/128/256ch FPS.
- CPU/GPU utilization.
- interaction smoothness under scroll/zoom/channel visibility changes.

If hardware 256ch FPS is below 30 FPS, revise the renderer before M2 using:

- visible-window-only upload,
- min/max or LTTB decimation,
- fewer draw calls via primitive restart or indexed drawing,
- persistent VBO reuse,
- shader-side channel offset/gain transforms.

## Reproduction Commands

Configure/build:

```bash
cd native/spike
conda run -n bf-qt6-spike cmake --fresh -B build -S . \
  -DCMAKE_PREFIX_PATH=/home/vsonic12138/miniconda3/envs/bf-qt6-spike \
  -DCMAKE_CXX_COMPILER=/home/vsonic12138/miniconda3/envs/bf-qt6-spike/bin/x86_64-conda-linux-gnu-c++
conda run -n bf-qt6-spike cmake --build build
```

IPC:

```bash
LD_LIBRARY_PATH=/home/vsonic12138/miniconda3/envs/bf-qt6-spike/lib:$LD_LIBRARY_PATH \
  ./build/ipc_bench \
  /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-eeg-upper-computer/.venv_build/bin/python \
  /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community/sidecar/spike/sidecar.py \
  synthetic://256x1000x200
```

Rendering hardware rerun:

```bash
./build/bf_spike --channels 256 --fs 1000 --window-sec 4 --duration-ms 10000
```
