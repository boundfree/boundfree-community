# BoundFree EEG Native M0 Spike

## Environment

- Date: 2026-07-05
- Host: WSL2 Ubuntu 22.04 on Linux `6.18.33.2-microsoft-standard-WSL2`
- System compiler: `g++ 11.4.0`
- Spike compiler: Conda `x86_64-conda-linux-gnu-c++ 15.2.0`
- System CMake: `3.22.1`
- Spike CMake: `4.2.3` from Conda env `bf-qt6-spike`
- Python: `3.10.12`
- Qt: `6.9.2` from `/home/vsonic12138/miniconda3/envs/bf-qt6-spike`
- Qt CMake prefix: `/home/vsonic12138/miniconda3/envs/bf-qt6-spike`
- vcpkg: not available on PATH for this WSL environment
- GPU reported by `lspci`: `Microsoft Corporation Device 008e`

## Toolchain Notes

The host has no system Qt6/qmake and `sudo` requires a password, so M0 uses a
user-space Conda Qt6 toolchain instead of installing OS packages. This keeps the
spike reproducible without modifying system directories.

Build commands should be run through:

```bash
conda run -n bf-qt6-spike cmake -B build -S . -DCMAKE_PREFIX_PATH=/home/vsonic12138/miniconda3/envs/bf-qt6-spike
conda run -n bf-qt6-spike cmake --build build
```

When configuring from a clean checkout, pass the Conda C++ compiler explicitly:

```bash
conda run -n bf-qt6-spike cmake --fresh -B build -S . \
  -DCMAKE_PREFIX_PATH=/home/vsonic12138/miniconda3/envs/bf-qt6-spike \
  -DCMAKE_CXX_COMPILER=/home/vsonic12138/miniconda3/envs/bf-qt6-spike/bin/x86_64-conda-linux-gnu-c++
```

## Spike-1 Rendering Result

The current WSL session has no `DISPLAY`, and Qt reports that `QOpenGLWidget` is
not supported on the `offscreen` platform. A user-space Xvfb server was used to
verify the OpenGL path with Mesa software rendering:

```bash
conda run -n bf-qt6-spike Xvfb :99 -screen 0 1280x800x24
conda run -n bf-qt6-spike env DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 ./build/bf_spike --channels 256 --duration-ms 2500
```

Observed software-rendered FPS-equivalent rates:

| Channels | Samples/window | Result |
| --- | ---: | --- |
| 64 | 4000 | 30 FPS first second, 47 FPS second second |
| 128 | 4000 | 27 FPS stable |
| 256 | 4000 | 19-21 FPS |

This confirms the spike executable and OpenGL rendering path are buildable and
runnable under a virtual display, but it is not a valid hardware GPU go/no-go
measurement. Real 64/128/256-channel FPS must be rerun on a machine with an
actual display/GPU context.

## Spike-2 IPC Result

Bench command:

```bash
LD_LIBRARY_PATH=/home/vsonic12138/miniconda3/envs/bf-qt6-spike/lib:$LD_LIBRARY_PATH \
  ./build/ipc_bench \
  /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-eeg-upper-computer/.venv_build/bin/python \
  /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community/sidecar/spike/sidecar.py \
  /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-eeg-upper-computer/samples/mark.edf
```

`mark.edf` result:

- Metadata: 1024 Hz, 3 channels, 1492 s, 2 markers.
- `open`: 18728 ms. This includes Python/MNE cold-start and EDF open cost.
- `get_slice(1s)`: average 0.26 ms over 100 iterations.
- Payload: 0.012 MiB per 1 s slice.
- Effective throughput: 45.07 MiB/s.

Synthetic bulk IPC result with `synthetic://256x1000x200`:

- Metadata: 1000 Hz, 256 channels, 200 s.
- `open`: 133-3136 ms observed, depending on process cold-start/cache state.
- `get_slice(1s)`: 2.94-4.82 ms average over 100 iterations.
- Payload: 0.977 MiB per 1 s slice.
- Effective throughput: 202.61-332.16 MiB/s.

The 256-channel 1 s payload is under the 30 ms soft-real-time target, so the
stdio + length-prefixed binary protocol is acceptable for M0. The cold `open`
cost should be hidden behind UI progress in M2 and does not affect steady-state
playback latency.
