# Python Baseline Profile

Date: 2026-07-05

Reference repo: `../boundfree-eeg-upper-computer`
Python env: `.venv_build/bin/python`

## Existing Performance Tests

Command:

```bash
.venv_build/bin/python -m pytest tests/test_playback_performance.py tests/test_waveform_performance.py -q
```

Result:

- `10 passed in 23.98s`
- Existing tests cover 64-channel waveform buffer stability, visible-curve refresh,
  time-axis cache reuse, file replay off-GUI-thread reads, and filter-bank batching.

## DSP Profile

Profile input:

- 256 channels
- 1000 Hz
- 10 s offline array: shape `[256, 10000]`
- 0.5 s realtime chunks: shape `[256, 500]`
- CAR reference
- Notch: 50/100/150 Hz
- Per-channel 1-100 Hz bandpass enabled for all 256 channels

Results:

| Path | Workload | Average |
| --- | --- | ---: |
| Offline DSP | 10 s, 256 ch | 434.12 ms |
| Realtime DSP | 0.5 s chunk, 256 ch | 14.51 ms |

Top cumulative costs:

- Offline path: `process_offline` total 2.170 s for 5 runs.
- Offline filtering: `offline_filter` / `scipy.signal.sosfiltfilt` about 1.18 s cumulative.
- Offline bandpass design is repeated per channel/run: `design_bandpass` / `scipy.signal.butter` about 0.96 s cumulative.
- Realtime path: `process_realtime` total 2.901 s for 200 chunks.
- Realtime filtering is dominated by repeated `scipy.signal.sosfilt`: 2.791 s cumulative.

Interpretation:

- The Python realtime DSP budget is acceptable for this synthetic 256ch/0.5s chunk
  test, but most CPU time is inside repeated SciPy calls and array-axis movement.
- Offline processing pays substantial repeated filter-design overhead; C++ should
  cache SOS coefficients and avoid per-call coefficient regeneration.
- Moving DSP to C++ remains justified for deterministic latency and true parallelism
  with rendering/recording, even where SciPy's vectorized kernels are fast.

## File Slice IO Baseline

Sample: `samples/mark.edf`

| Operation | Result |
| --- | ---: |
| `read_file_lazy()` open | 42574.27 ms |
| 100x `get_slice(1s)` average | 0.118 ms |
| Slice shape | `[3, 1024]` |

Interpretation:

- EDF/MNE cold open is expensive and must be represented in the UI as an async
  operation with progress/busy state.
- Steady-state lazy slicing for a small EDF is fast; IPC adds measurable but
  acceptable overhead compared with in-process Python slicing.

## Rendering Baseline

The existing Python performance tests verify 64-channel waveform refresh stays
under the current 80 ms budget in offscreen tests. This does not directly measure
256-channel interactive GUI FPS. M0's C++ OpenGL spike therefore remains the
primary rendering-risk gate, and must be rerun on a machine with a real display
and GPU context because this WSL session only supports Xvfb software rendering.

## Baseline Conclusion

The old Python stack is functionally stable and has useful optimized paths, but:

- MNE open latency is high enough that file open must be asynchronous in the new UI.
- DSP hotspots are concentrated in repeated SciPy filtering/filter design calls.
- Rendering scalability beyond 64 channels is not proven by the old tests.
- The native rewrite should proceed only after hardware GPU FPS is confirmed, while
  IPC throughput already looks sufficient for 256ch 1 s slices.
