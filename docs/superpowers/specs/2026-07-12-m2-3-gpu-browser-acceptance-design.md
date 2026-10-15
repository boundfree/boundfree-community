---
title: M2-3 GPU And Browser Acceptance Design
doc_type: design
status: draft
owner: BoundFree
last_updated: 2026-07-12
---

# M2-3 GPU And Browser Acceptance Design

## Context

M2-2 delivered the static waveform browser foundation on `master`: explicit request generation, waveform view state, visible-channel display model, basic toolbar controls, latest-request-wins smoke coverage, offscreen render benchmark CLI, and format evidence reporting. The M2 real GPU/display gate remains open, and M2 still cannot be marked complete.

M2-3 should close the practical browser acceptance gap before M3 DSP work begins. The goal is not to add analysis features, but to prove that the file-open and static browsing surface is usable, measurable, and honestly documented.

## Goals

- Run or prepare a repeatable real GPU/display benchmark flow for M2 waveform rendering.
- Record GPU gate results in `docs/reports/m2-rendering-gpu-gate.md` without treating WSL, Xvfb, Mesa, or offscreen results as acceptance.
- Harden the static browser interaction surface: channel list visibility, vertical channel scrolling, time-window changes, amplitude changes, and visible-channel status.
- Ensure window changes request a fresh slice and stale slice results cannot overwrite newer browser state.
- Keep M2 status conservative in `docs/roadmap.md` and `docs/feature-parity-checklist.md`.
- Preserve v1 scope: general EEG/LFP/electrophysiology file analysis; self-developed ADS1299/ESP32-C3 BLE acquisition remains v1.1/v2.

## Non-Goals

- No DSP implementation: raw/CAR/bipolar reference, notch, bandpass, PSD, STFT, band power, and coherence remain M3/M4.
- No playback engine, marker editing, video sync, export, recording, packaging, or hardware acquisition implementation.
- No direct Native parsing of scientific file formats; file ownership remains in the Python sidecar.
- No public support claim for sample-gated formats unless recorded evidence exists.
- No claim that M2 is fully complete unless the real GPU/display gate is recorded as passed.

## Recommended Approach

M2-3 should be implemented as a browser-acceptance slice, not as a broad feature expansion. The work should proceed in this order:

1. Add a clearer GPU benchmark result capture path and documentation checklist.
2. Add a channel browser panel that controls the existing `WaveformViewState`.
3. Connect time-window changes to explicit `openWindow` slice requests.
4. Add smoke tests for channel visibility, scroll/window state, and stale slice protection.
5. Update reports and roadmap after verification.

This order keeps the highest-risk gate visible while making the browser surface testable enough to decide whether M2 can close or must remain blocked by rendering performance.

## Browser Behavior

The M2-3 browser should remain utilitarian. It should expose enough controls for real file inspection without becoming playback or analysis UI:

- Channel list shows opened file channel names.
- Each channel can be toggled visible or hidden.
- The first visible channel / vertical scroll offset can change without rebuilding the file session.
- Time window remains `1-120s`, default `10s`.
- Amplitude scale remains default `+-200uV` and supports user adjustment.
- Status text includes format, visible time interval, amplitude scale, and visible channel count.
- Window changes request a new sidecar slice with the active file session/path and latest-request-wins semantics.

If a browser interaction cannot yet be wired to real file data without larger refactoring, it should be represented as an explicit smoke-tested state transition and documented as not full M2 closure.

## Rendering Gate

M2-3 must keep a strict distinction between development smoke and acceptance:

- Offscreen benchmark smoke can prove the CLI path works.
- Real GPU/display benchmark is required for M2 acceptance.
- Required scenarios remain 64, 128, and 256 channels at 1 kHz with a 4 s visible window.
- Acceptance target remains `>=55 FPS` preferred for 256 channels, `>=30 FPS` only if interaction remains smooth.
- Below `30 FPS` blocks full M2 waveform acceptance and requires rendering redesign before M3.

The report must include OS, GPU, driver, Qt version, command, FPS, CPU/GPU usage, and subjective notes for scroll, zoom, and channel visibility.

## Architecture

M2-3 should extend the current M2-2 boundaries:

- `WaveformViewState` remains the browser state source for window, amplitude, first visible channel, and hidden channels.
- `WaveformDisplayModel` remains the render-facing model; UI controls should update state rather than manipulating samples directly.
- `FileOpenController::openWindow` remains the sidecar slice request entry point.
- `RequestGeneration` remains the stale-result guard for async open/window changes.
- `MainWindow` may own simple widget wiring, but file IO, DSP, export, and format parsing stay out of UI code.

If UI code grows too large, M2-3 may introduce a small controller/helper for browser controls, but only if it reduces real coupling and keeps tests focused.

## Error Handling

- Sidecar and backend errors remain `code + params`.
- Stale errors must not replace the active browser status.
- Window requests outside file duration should surface `reader.slice_out_of_range`.
- Missing dependencies or sample-gated formats must remain non-acceptance evidence.

## Testing Strategy

Tests should prove behavior without pretending to close hardware-dependent gates:

- Native smoke test for channel panel state and visible-channel count.
- Native smoke test for time-window control requesting the expected slice interval.
- Native smoke test for latest-window result winning over stale window requests.
- Existing offscreen render benchmark smoke remains required.
- Real GPU/display run is recorded manually or semi-manually in the report; it is not replaced by offscreen CI.

Final verification should include:

```bash
cmake --build native/build
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
./scripts/wsl/verify_m1_foundation.sh --fast
python3 scripts/common/collect_m2_format_evidence.py --dry-run
git diff --check HEAD
```

## Documentation Updates

M2-3 should update:

- `docs/reports/m2-rendering-gpu-gate.md`: benchmark result rows and gate status.
- `docs/feature-parity-checklist.md`: only mark browser items backed by implementation and evidence.
- `docs/roadmap.md`: update current phase after verification.
- `docs/superpowers/plans/`: implementation plan with completed checkboxes after execution.

Documentation must not mark DSP, playback, annotations, video, export, recording, packaging, or hardware acquisition complete.

## Risks And Mitigations

- Real GPU gate may fail. Mitigation: keep the result visible and plan rendering redesign before M3.
- UI scope may expand into playback or analysis. Mitigation: keep M2-3 to static browsing and acceptance only.
- Channel controls can become tangled with rendering internals. Mitigation: update `WaveformViewState`, then render through `WaveformDisplayModel`.
- Manual GPU evidence may be unavailable in the current WSL environment. Mitigation: keep the gate open and provide an exact command/template for the real environment.

## Acceptance Criteria

M2-3 is complete when:

- Browser channel visibility and vertical browsing state are smoke-tested.
- Time-window changes can request and display the expected slice interval.
- Latest-request-wins behavior is covered for window changes.
- GPU benchmark report records a real GPU/display result, or explicitly keeps the gate open with the missing evidence documented.
- M2 checklist and roadmap reflect only verified progress.
- Full verification passes with optional dependency skips treated as non-evidence.
