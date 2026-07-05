# BoundFree EEG Native M0 Spike

## Environment

- Date: 2026-07-05
- Host: WSL2 Ubuntu 22.04 on Linux `6.18.33.2-microsoft-standard-WSL2`
- CPU/compiler: `g++ 11.4.0`
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
