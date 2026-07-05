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
