---
title: M2-4 Real GPU Display Gate Design
doc_type: design
status: superseded
original_status: draft
owner: BoundFree
last_updated: 2026-07-12
archived_at: 2026-10-23
---

# M2-4 Real GPU Display Gate Design

## Context

M2-3 is merged to `master`. It added a machine-readable waveform benchmark smoke, browser channel controls, time-window request wiring, latest-request-wins coverage, and conservative M2 documentation. The remaining M2 blocker is the real GPU/display gate in `docs/reports/m2-rendering-gpu-gate.md`.

The current workspace is WSL/offscreen-oriented. Results from WSL, Xvfb, Mesa software rendering, or `QT_QPA_PLATFORM=offscreen` are development signals only. M2-4 must make the real GPU gate executable and auditable, but must not mark it passed unless a real Windows or Linux desktop GPU/display run is recorded.

## Goals

- Provide a repeatable real GPU benchmark command flow for 64, 128, and 256 channels at 1 kHz with a 4 s visible window.
- Capture benchmark results in machine-readable JSON and a human-readable report table.
- Capture enough host environment metadata to make the result auditable: OS/display environment, GPU model, driver version, Qt version, command, FPS, CPU/GPU observation source, and interaction notes.
- Keep development smoke and acceptance evidence separate.
- Update roadmap/checklist/report status conservatively after the gate run or after preparing the runbook.
- Define the decision path if the gate passes, is marginal, fails, or cannot be run in the current environment.

## Non-Goals

- No DSP, playback, event marker, video sync, export, recording, packaging, or hardware acquisition work.
- No direct Native scientific file parsing.
- No claim that M2 is complete from offscreen/WSL/Mesa results.
- No automatic GPU utilization collection that requires platform-specific privileged tooling. M2-4 may document manual sources such as Task Manager, vendor tools, `nvidia-smi`, or system monitor.
- No rendering rewrite unless real GPU evidence shows the current renderer is blocked.

## Recommended Approach

M2-4 should produce a small evidence pack instead of another browser feature:

1. Add or extend a benchmark runner that invokes `bf_app --smoke-waveform-benchmark --json` for 64, 128, and 256 channels.
2. Make the runner refuse to label offscreen/WSL/Xvfb/Mesa results as acceptance.
3. Add a report updater or a clearly structured output block that can be pasted into `docs/reports/m2-rendering-gpu-gate.md`.
4. Add tests around JSON parsing, gate classification, and report formatting using synthetic benchmark output.
5. Run full repository verification and update docs.

This gives the project a stable gate process even if the current machine cannot close the gate.

## Benchmark Scenarios

Required scenarios:

- `64ch @ 1000Hz, 4s, 120 frames`
- `128ch @ 1000Hz, 4s, 120 frames`
- `256ch @ 1000Hz, 4s, 120 frames`

Development command template:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 30 --json
```

Real GPU command template:

```bash
native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 120 --json
```

The real GPU run must use an actual desktop display platform, for example Windows desktop or Linux X11/Wayland with GPU acceleration. It must not set `QT_QPA_PLATFORM=offscreen`.

## Gate Classification

The report should classify each full scenario set:

- `Passed`: 256-channel FPS is at least 55 and interaction notes do not report visible stalls.
- `Accepted with note`: 256-channel FPS is at least 30, all three scenarios ran, and interaction notes say scrolling, zooming, and channel visibility remain usable.
- `Blocked`: 256-channel FPS is below 30, any scenario fails to run, or interaction notes report visible stalls.
- `Open`: real GPU/display evidence is unavailable or the run happened in offscreen/WSL/Xvfb/Mesa.

M2 can only close the waveform browser GPU gate for `Passed` or `Accepted with note`.

## Evidence Format

The runner/report should preserve these fields:

- Date.
- Environment label.
- OS and display environment.
- GPU model and driver version.
- Qt version.
- Command.
- Per-scenario FPS for 64, 128, and 256 channels.
- CPU observation and source.
- GPU observation and source.
- Interaction notes for scroll, zoom, and channel visibility.
- Gate classification.

The report table in `docs/reports/m2-rendering-gpu-gate.md` may stay compact, but the detailed evidence block must retain the full command and host metadata.

## Architecture

M2-4 should keep responsibilities small:

- `native/src/app/main.cpp` remains the benchmark producer through `--smoke-waveform-benchmark --json`.
- A script under `scripts/` may orchestrate multi-scenario runs and format evidence. It should not parse scientific data files.
- Tests under `tests/` should validate parsing and classification with synthetic JSON output rather than relying on a real GPU.
- `docs/reports/m2-rendering-gpu-gate.md` remains the canonical gate record.

If a new script is added, it should accept explicit flags for environment label and interaction notes so the user can run it on a real host and capture the needed context without editing code.

## Error Handling

- Invalid benchmark JSON should produce a clear nonzero script result and not update acceptance status.
- Missing `bf_app` should produce an actionable path/build error.
- Offscreen/WSL/Xvfb/Mesa detection should force `Gate: Open` or `development-signal-only`.
- If one scenario fails, the scenario set is `Blocked` for acceptance until rerun successfully.
- If optional CPU/GPU utilization is unavailable, the report must say `not recorded` and cannot be upgraded above `Accepted with note`.

## Testing Strategy

Automated tests should cover:

- Parsing benchmark JSON for channels, fs, seconds, frames, fps, and gate.
- Aggregating 64/128/256 scenario results.
- Gate classification for pass, marginal, blocked, and open/development-signal-only cases.
- Report row or evidence block formatting.
- Existing native benchmark smoke still passes.

Manual validation on a real GPU host should run:

```bash
cmake --build native/build
native/build/bf_app --smoke-waveform-benchmark --channels 64 --fs 1000 --seconds 4 --frames 120 --json
native/build/bf_app --smoke-waveform-benchmark --channels 128 --fs 1000 --seconds 4 --frames 120 --json
native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 120 --json
```

Repository verification remains:

```bash
cmake --build native/build
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
./scripts/wsl/verify_m1_foundation.sh --fast
python3 scripts/common/collect_m2_format_evidence.py --dry-run
git diff --check HEAD
```

## Documentation Updates

M2-4 should update:

- `docs/reports/m2-rendering-gpu-gate.md`: add the runbook, evidence block, and current classification.
- `docs/feature-parity-checklist.md`: check the real GPU row only if real evidence passes or is accepted with note.
- `docs/roadmap.md`: move from M2-3 to M2-4 during work; only mark M2 browser gate closed if evidence supports it.
- `docs/superpowers/plans/`: add the implementation plan and mark completed steps after verification.

## Risks And Mitigations

- Real GPU host is not available in this session. Mitigation: finish the evidence runner and keep the gate `Open`.
- The benchmark passes offscreen but fails on a real host. Mitigation: classify as `Blocked` and create a follow-up rendering optimization plan before M3.
- Results are not reproducible because environment metadata is incomplete. Mitigation: require OS/display/GPU/driver/Qt/command in the evidence block.
- Manual interaction notes are too vague. Mitigation: require separate scroll, zoom, and channel-visibility notes.

## Acceptance Criteria

M2-4 is complete when:

- The real GPU benchmark process is documented and repeatable.
- Development/offscreen evidence cannot be recorded as acceptance by mistake.
- 64/128/256 benchmark result aggregation and gate classification are tested.
- `docs/reports/m2-rendering-gpu-gate.md` clearly shows either real GPU evidence or why the gate remains open.
- `docs/feature-parity-checklist.md` and `docs/roadmap.md` remain conservative.
- Full repository verification passes.
