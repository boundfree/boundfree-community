---
title: M2-2 Static Waveform Browser Design
doc_type: design
status: draft
owner: BoundFree
last_updated: 2026-07-08
---

# M2-2 Static Waveform Browser Design

## Context

M2-1 established the file slice contract, dependency-gated reader paths, a Native `getSlice` bridge, MinMax decimation, and a first static waveform entry. The next stage should finish the M2 browser surface before moving into DSP or analysis features.

M2-2 is the product-quality closure for "open a supported file, browse static waveforms, and know which format/rendering claims are backed by evidence." It keeps the v1 boundary unchanged: v1 stable is a C++/Qt6 Native general EEG/LFP/electrophysiology file analysis product, while self-developed ADS1299/ESP32-C3 BLE acquisition remains v1.1/v2 scope.

## Goals

- Complete static waveform browsing controls for opened files.
- Prove or explicitly defer the real GPU/display gate for M2.
- Strengthen async open and slice request behavior so stale results cannot overwrite newer file/window state.
- Convert HDF5 and MNE dependency-gated paths into recorded evidence when dependencies and samples are available.
- Keep public support claims narrow: a format is only marked complete when acceptance evidence exists.
- Update roadmap, feature checklist, and M2 reports to reflect the real state.

## Non-Goals

- No DSP implementation: raw/CAR/bipolar reference, notch, bandpass, PSD, STFT, band power, and coherence remain M3/M4.
- No playback engine, annotation editing, video sync, segment export, packaging, or recorder implementation.
- No self-developed hardware acquisition, BLE device protocol, real device recording, or long-duration acquisition testing.
- No direct Native parsing of scientific file formats; file format ownership remains in the Python sidecar.

## Recommended Approach

Implement M2-2 in five slices:

1. Rendering gate and instrumentation.
2. Waveform browser interaction model.
3. Async open and latest-request-wins hardening.
4. Format evidence capture for dependency-gated readers.
5. Documentation and checklist closure.

This order prevents the project from building deeper analysis features on an unproven browser foundation. It also keeps the highest-risk gate, real rendering performance, visible before expanding UI behavior.

## Architecture

The Python sidecar remains responsible for `open` and `get_slice`. It returns metadata, structured errors, and `[n_channels, n_samples]` data in `uV`. Native owns session state, current view state, slice request scheduling, decimation, and presentation.

Native should separate browser state into explicit units:

- File session state: current handle, file path, metadata, and lifecycle.
- View state: visible time window, amplitude range, selected/visible channels, scroll offset, and mode label.
- Request state: monotonically increasing request id or generation token for file/window changes.
- Render model: decimated channel data plus axis information, independent from widgets.

The GUI thread must not perform file IO, DSP, export, or long-running analysis. User actions update view/request state, then dispatch sidecar work through an async boundary. When results return, Native applies them only if they match the current request generation.

## Browser Behavior

M2-2 should make the static browser useful without becoming a full playback or analysis system:

- Multi-channel waveform display supports vertical scrolling.
- Channel visibility can be toggled per channel.
- Time window supports `1-120s`, with default `10s`.
- Amplitude scale defaults to `+-200uV` and can be changed by the user.
- Status text shows file name, mode, visible time window, and failure state.
- Same-window redraws should reuse stable axis/render resources where practical.
- Large windows or high sample rates continue to use MinMax, LTTB, or an equivalent decimation strategy.

The first M2-2 UI may be utilitarian: toolbar controls, channel panel, waveform viewport, and status bar are enough. It should not add marketing pages, tutorial text, or unrelated dashboard surfaces.

## Rendering Gate

The M2 real rendering gate must be tested on a real Windows or Linux GPU/display environment and recorded in `docs/reports/m2-rendering-gpu-gate.md`.

Required scenarios:

- 64, 128, and 256 channels.
- 1 kHz sampling rate.
- 4 s visible window.
- Record FPS, CPU/GPU usage, and subjective smoothness for scroll, zoom, and channel visibility.

Acceptance target:

- Prefer `>=55 FPS` for 256ch @ 1 kHz with a 4 s window.
- `>=30 FPS` can pass only if interaction remains smooth.
- Below `30 FPS` blocks full M2 waveform acceptance and requires rendering redesign before moving beyond M2.

WSL, Xvfb, and Mesa results may remain development signals, but they cannot satisfy the real GPU/display gate.

## Async And Stale Result Handling

M2-1 introduced async open and a basic browser entry. M2-2 should harden this into explicit behavior:

- Opening a new file cancels or invalidates older open/slice results.
- Changing the time window invalidates older slice results.
- Quick repeated file switches use latest-request-wins.
- Quick repeated window changes use latest-request-wins.
- Errors from stale requests are ignored unless they belong to the active generation.
- UI busy state is tied to the active request, not any background request.

The test suite should include overlap cases where an older operation completes after a newer one and must not overwrite the current session or waveform.

## Format Evidence

M2-1 added reader paths and dependency gates. M2-2 should separate "path exists" from "accepted":

- CSV remains accepted through generated fixture tests.
- HDF5 can be accepted only when `h5py==3.16.0` tests run and pass in an environment where the dependency is installed.
- EDF can be accepted only when `mne==1.12.1` and an accepted sample are available and tests run.
- BDF, BrainVision, EEGLAB, and FIF remain gated until real or generated acceptance samples are recorded.
- Intan RHS and BBK/DAT/LSD remain unaccepted until implemented and tested.

`docs/reports/m2-format-acceptance.md` should record the command, environment, sample source, and result for each accepted format. Skipped tests are not acceptance evidence.

## Error Handling

Sidecar errors continue to cross IPC as structured codes and params. Native maps those codes through i18n. M2-2 should preserve this boundary:

- No localized text from the sidecar.
- Missing files, unsupported formats, dependency failures, open failures, invalid slice params, and session-not-found errors remain structured.
- UI displays actionable state without losing the raw code in logs or diagnostics.
- Stale errors do not replace the current active status.

## Testing Strategy

Tests should stay focused on behavior:

- Native smoke tests for browser controls and state transitions.
- Native controller tests for latest-request-wins file/window behavior.
- Sidecar tests for dependency-gated format evidence.
- Offscreen render smoke remains useful, but it does not close the real GPU gate.
- Manual or scripted real GPU run records acceptance data in the M2 rendering report.

The final M2-2 verification set should include:

```bash
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
cmake --build native/build
./scripts/verify_m1_foundation.sh --fast
git diff --check HEAD
```

If optional dependencies are installed for format evidence, run the relevant HDF5/MNE tests and record their exact command output in the acceptance report.

## Documentation Updates

M2-2 should update these docs:

- `docs/feature-parity-checklist.md`: mark only completed browser and format items.
- `docs/reports/m2-rendering-gpu-gate.md`: record real GPU/display results or explicitly keep the gate open.
- `docs/reports/m2-format-acceptance.md`: record dependency/sample evidence.
- `docs/roadmap.md`: update current phase after verification.

No document should imply full M2 completion unless rendering, browser interaction, and format evidence gates are actually satisfied.

## Risks And Mitigations

- Real GPU gate may fail. Mitigation: treat failure as a rendering task, not as documentation debt.
- Optional format dependencies may be unavailable in the current environment. Mitigation: keep dependency-gated tests and document skipped runs as non-evidence.
- Latest-request-wins logic may become fragile if state is implicit. Mitigation: use an explicit generation token and tests for out-of-order completions.
- UI scope can expand quickly. Mitigation: keep M2-2 to static browser controls and defer playback, annotations, and DSP.

## Acceptance Criteria

M2-2 is complete when:

- Static waveform browser supports vertical scrolling, channel visibility, time window, amplitude scale, and status display.
- Async open/window changes ignore stale results.
- Real GPU/display gate is recorded and either passes or blocks full M2 with a documented reason.
- Format acceptance report clearly distinguishes accepted, dependency-gated, and unimplemented formats.
- Checklist and roadmap match the verified state.
- Full verification passes, allowing known optional-dependency skips only when documented as non-evidence.
