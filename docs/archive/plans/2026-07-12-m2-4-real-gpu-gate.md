---
title: M2-4 Real GPU Display Gate Implementation Plan
doc_type: architecture
status: superseded
owner: BoundFree
last_updated: 2026-10-23
archived_at: 2026-10-23
---

# M2-4 Real GPU Display Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a repeatable, conservative real GPU/display gate evidence flow for M2 waveform rendering without treating WSL/offscreen development results as acceptance.

**Architecture:** Keep `bf_app --smoke-waveform-benchmark --json` as the benchmark producer and add a small Python evidence script under `scripts/` for scenario orchestration, JSON parsing, gate classification, and report-block formatting. Tests use synthetic benchmark JSON so CI does not depend on a real GPU, while docs keep the canonical gate status in `docs/reports/m2-rendering-gpu-gate.md`.

**Tech Stack:** C++17/Qt6 benchmark producer already present, Python 3.10+ standard library, pytest, existing docs under `docs/reports/`, `docs/roadmap.md`, and `docs/feature-parity-checklist.md`.

## Global Constraints

- Work in `boundfree-community`; do not modify `../boundfree-eeg-upper-computer`.
- Current product version remains `0.1.0-dev`; ordinary M2-4 work must not change `version.json`.
- No DSP, playback, event marker, video sync, export, recording, packaging, or hardware acquisition work.
- No direct Native scientific file parsing.
- No claim that M2 is complete from offscreen/WSL/Mesa results.
- No automatic GPU utilization collection that requires platform-specific privileged tooling.
- No rendering rewrite unless real GPU evidence shows the current renderer is blocked.
- Required scenarios are 64, 128, and 256 channels at 1 kHz with a 4 s visible window.
- Real GPU run must use an actual desktop display platform and must not set `QT_QPA_PLATFORM=offscreen`.
- M2 can only close the waveform browser GPU gate for `Passed` or `Accepted with note`.
- Development/offscreen evidence cannot be recorded as acceptance by mistake.
- Follow TDD: write the failing test, verify it fails, implement the minimal behavior, verify it passes, then commit.

---

## File Structure

| Path | Responsibility |
| --- | --- |
| `scripts/common/collect_m2_gpu_gate_evidence.py` | New script that parses benchmark JSON, detects development-only environments, classifies M2 GPU gate status, optionally runs benchmark scenarios, and prints JSON plus Markdown evidence blocks. |
| `tests/scripts/test_m2_gpu_gate_evidence.py` | New pytest coverage for parsing, aggregation, classification, dry-run behavior, and report-block formatting. |
| `tests/native/test_bf_app.py` | Existing native smoke coverage remains the source of truth for the `bf_app --smoke-waveform-benchmark --json` producer. |
| `docs/reports/m2-rendering-gpu-gate.md` | Canonical M2 GPU gate record; add M2-4 runbook, evidence format, and conservative status. |
| `docs/feature-parity-checklist.md` | Keep real GPU checklist unchecked unless real evidence is recorded as passed or accepted with note. |
| `docs/roadmap.md` | Move current phase from M2-3 to M2-4 and keep M2 gate conservative. |
| `docs/superpowers/plans/2026-07-12-m2-4-real-gpu-gate.md` | Track task execution checkboxes after verification. |

---

### Task 1: GPU Gate Evidence Model And Classification

**Files:**
- Create: `scripts/common/collect_m2_gpu_gate_evidence.py`
- Create: `tests/scripts/test_m2_gpu_gate_evidence.py`

**Interfaces:**
- Produces: `BenchmarkResult`, `EvidenceMetadata`, `parse_benchmark_json(text: str) -> BenchmarkResult`, `classify_gate(results: list[BenchmarkResult], metadata: EvidenceMetadata, development_reasons: list[str]) -> str`, and `format_markdown_evidence(results: list[BenchmarkResult], metadata: EvidenceMetadata, gate: str, development_reasons: list[str]) -> str`.
- Later tasks consume these functions from the same script for CLI behavior and report formatting.

- [x] **Step 1: Write failing model/classification tests**

Create `tests/scripts/test_m2_gpu_gate_evidence.py` with this content:

```python
import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "collect_m2_gpu_gate_evidence.py"

spec = importlib.util.spec_from_file_location("collect_m2_gpu_gate_evidence", SCRIPT)
gpu_gate = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = gpu_gate
spec.loader.exec_module(gpu_gate)


def _result(channels: int, fps: float):
    return gpu_gate.BenchmarkResult(
        channels=channels,
        fs=1000,
        seconds=4,
        frames=120,
        fps=fps,
        producer_gate="development-signal-only",
        command=f"bf_app --smoke-waveform-benchmark --channels {channels}",
    )


def _metadata(**overrides):
    values = {
        "date": "2026-07-12",
        "environment_label": "real-linux-gpu",
        "os_display": "Ubuntu 24.04 Wayland",
        "gpu_model": "NVIDIA RTX 4060",
        "driver_version": "555.42",
        "qt_version": "6.6.3",
        "cpu_observation": "12% via system monitor",
        "gpu_observation": "38% via nvidia-smi",
        "scroll_note": "smooth",
        "zoom_note": "smooth",
        "channel_visibility_note": "smooth",
    }
    values.update(overrides)
    return gpu_gate.EvidenceMetadata(**values)


def test_parse_benchmark_json_reads_required_fields():
    parsed = gpu_gate.parse_benchmark_json(
        json.dumps(
            {
                "channels": 256,
                "fs": 1000,
                "seconds": 4,
                "frames": 120,
                "fps": 58.5,
                "gate": "development-signal-only",
            }
        )
    )

    assert parsed.channels == 256
    assert parsed.fs == 1000
    assert parsed.seconds == 4
    assert parsed.frames == 120
    assert parsed.fps == 58.5
    assert parsed.producer_gate == "development-signal-only"


def test_classify_gate_passed_when_256_channel_fps_is_at_least_55():
    gate = gpu_gate.classify_gate(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 55.0)],
        _metadata(),
        [],
    )

    assert gate == "Passed"


def test_classify_gate_accepts_with_note_between_30_and_55_fps():
    gate = gpu_gate.classify_gate(
        [_result(64, 90.0), _result(128, 62.0), _result(256, 31.0)],
        _metadata(),
        [],
    )

    assert gate == "Accepted with note"


def test_classify_gate_blocks_below_30_fps_or_stalls():
    low_fps_gate = gpu_gate.classify_gate(
        [_result(64, 90.0), _result(128, 60.0), _result(256, 29.9)],
        _metadata(),
        [],
    )
    stall_gate = gpu_gate.classify_gate(
        [_result(64, 90.0), _result(128, 60.0), _result(256, 45.0)],
        _metadata(scroll_note="visible stall during scroll"),
        [],
    )

    assert low_fps_gate == "Blocked"
    assert stall_gate == "Blocked"


def test_classify_gate_open_for_development_environment_or_missing_metadata():
    development_gate = gpu_gate.classify_gate(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 70.0)],
        _metadata(),
        ["QT_QPA_PLATFORM=offscreen"],
    )
    missing_metadata_gate = gpu_gate.classify_gate(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 70.0)],
        _metadata(gpu_observation="not recorded"),
        [],
    )

    assert development_gate == "Open"
    assert missing_metadata_gate == "Accepted with note"


def test_format_markdown_evidence_preserves_metadata_and_scenarios():
    block = gpu_gate.format_markdown_evidence(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 55.0)],
        _metadata(),
        "Passed",
        [],
    )

    assert "### 2026-07-12 real-linux-gpu" in block
    assert "- Gate: Passed" in block
    assert "- OS/display: Ubuntu 24.04 Wayland" in block
    assert "- GPU: NVIDIA RTX 4060, driver 555.42" in block
    assert "| 256 | 1000 | 4 | 120 | 55.00 |" in block
    assert "- Channel visibility: smooth" in block
```

- [x] **Step 2: Run tests and verify they fail**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py -q
```

Expected: FAIL because `scripts/common/collect_m2_gpu_gate_evidence.py` does not exist.

- [x] **Step 3: Implement model, parsing, classification, and Markdown formatting**

Create `scripts/common/collect_m2_gpu_gate_evidence.py` with this content:

```python
#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
import json


REQUIRED_CHANNELS = (64, 128, 256)


@dataclass(frozen=True)
class BenchmarkResult:
    channels: int
    fs: int
    seconds: int
    frames: int
    fps: float
    producer_gate: str
    command: str = ""


@dataclass(frozen=True)
class EvidenceMetadata:
    date: str
    environment_label: str
    os_display: str
    gpu_model: str
    driver_version: str
    qt_version: str
    cpu_observation: str
    gpu_observation: str
    scroll_note: str
    zoom_note: str
    channel_visibility_note: str


def parse_benchmark_json(text: str, command: str = "") -> BenchmarkResult:
    payload = json.loads(text)
    return BenchmarkResult(
        channels=int(payload["channels"]),
        fs=int(payload["fs"]),
        seconds=int(payload["seconds"]),
        frames=int(payload["frames"]),
        fps=float(payload["fps"]),
        producer_gate=str(payload.get("gate", "")),
        command=command,
    )


def _scenario_map(results: list[BenchmarkResult]) -> dict[int, BenchmarkResult]:
    return {result.channels: result for result in results}


def _has_stall(metadata: EvidenceMetadata) -> bool:
    notes = " ".join(
        [
            metadata.scroll_note,
            metadata.zoom_note,
            metadata.channel_visibility_note,
        ]
    ).lower()
    return "stall" in notes or "卡顿" in notes


def _missing_required_metadata(metadata: EvidenceMetadata) -> bool:
    required = [
        metadata.date,
        metadata.environment_label,
        metadata.os_display,
        metadata.gpu_model,
        metadata.driver_version,
        metadata.qt_version,
        metadata.cpu_observation,
        metadata.gpu_observation,
        metadata.scroll_note,
        metadata.zoom_note,
        metadata.channel_visibility_note,
    ]
    return any(not value.strip() for value in required)


def _has_unrecorded_utilization(metadata: EvidenceMetadata) -> bool:
    return (
        metadata.cpu_observation.strip().lower() == "not recorded"
        or metadata.gpu_observation.strip().lower() == "not recorded"
    )


def classify_gate(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    development_reasons: list[str],
) -> str:
    if development_reasons:
        return "Open"
    if _missing_required_metadata(metadata):
        return "Open"

    scenarios = _scenario_map(results)
    if any(channel not in scenarios for channel in REQUIRED_CHANNELS):
        return "Blocked"
    if any(
        scenarios[channel].fs != 1000 or scenarios[channel].seconds != 4
        for channel in REQUIRED_CHANNELS
    ):
        return "Blocked"
    if _has_stall(metadata):
        return "Blocked"

    fps_256 = scenarios[256].fps
    if fps_256 < 30.0:
        return "Blocked"
    if fps_256 >= 55.0 and not _has_unrecorded_utilization(metadata):
        return "Passed"
    return "Accepted with note"


def format_markdown_evidence(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    gate: str,
    development_reasons: list[str],
) -> str:
    scenarios = sorted(results, key=lambda result: result.channels)
    lines = [
        f"### {metadata.date} {metadata.environment_label}",
        "",
        f"- Gate: {gate}",
        f"- OS/display: {metadata.os_display}",
        f"- GPU: {metadata.gpu_model}, driver {metadata.driver_version}",
        f"- Qt: {metadata.qt_version}",
        f"- CPU: {metadata.cpu_observation}",
        f"- GPU usage: {metadata.gpu_observation}",
    ]
    if development_reasons:
        lines.append(f"- Development-only reasons: {', '.join(development_reasons)}")
    lines.extend(
        [
            "",
            "| Channels | fs | Window | Frames | FPS |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for result in scenarios:
        lines.append(
            f"| {result.channels} | {result.fs} | {result.seconds} | "
            f"{result.frames} | {result.fps:.2f} |"
        )
    lines.extend(
        [
            "",
            "Interaction notes:",
            "",
            f"- Scroll: {metadata.scroll_note}",
            f"- Zoom: {metadata.zoom_note}",
            f"- Channel visibility: {metadata.channel_visibility_note}",
        ]
    )
    commands = [result.command for result in scenarios if result.command]
    if commands:
        lines.extend(["", "Commands:", ""])
        lines.extend(f"- `{command}`" for command in commands)
    return "\n".join(lines)
```

- [x] **Step 4: Run tests and verify they pass**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py -q
```

Expected: `6 passed`.

- [x] **Step 5: Commit**

Run:

```bash
git add scripts/common/collect_m2_gpu_gate_evidence.py tests/scripts/test_m2_gpu_gate_evidence.py
git commit -m "feat: add m2 gpu gate evidence model"
```

---

### Task 2: GPU Gate Evidence CLI And Scenario Runner

**Files:**
- Modify: `scripts/common/collect_m2_gpu_gate_evidence.py`
- Modify: `tests/scripts/test_m2_gpu_gate_evidence.py`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: Task 1 model functions.
- Produces: CLI options `--dry-run`, `--from-json`, `--run`, `--acceptance-run`, `--app`, `--frames`, `--environment-label`, `--os-display`, `--gpu-model`, `--driver-version`, `--qt-version`, `--cpu-observation`, `--gpu-observation`, `--scroll-note`, `--zoom-note`, `--channel-visibility-note`, and JSON output with `gate`, `development_reasons`, `results`, and `markdown`.

- [x] **Step 1: Write failing CLI tests**

Append these tests to `tests/scripts/test_m2_gpu_gate_evidence.py`:

```python
import os
import subprocess


def test_cli_dry_run_reports_open_and_commands():
    result = subprocess.run(
        ["python3", str(SCRIPT), "--dry-run"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )

    data = json.loads(result.stdout)
    assert data["gate"] == "Open"
    assert data["development_reasons"] == ["dry-run"]
    assert [item["channels"] for item in data["results"]] == [64, 128, 256]
    assert "--channels 256" in data["markdown"]


def test_cli_from_json_refuses_offscreen_acceptance(tmp_path):
    payload = tmp_path / "benchmarks.json"
    payload.write_text(
        json.dumps(
            [
                {"channels": 64, "fs": 1000, "seconds": 4, "frames": 120, "fps": 120.0, "gate": "development-signal-only"},
                {"channels": 128, "fs": 1000, "seconds": 4, "frames": 120, "fps": 90.0, "gate": "development-signal-only"},
                {"channels": 256, "fs": 1000, "seconds": 4, "frames": 120, "fps": 70.0, "gate": "development-signal-only"},
            ]
        ),
        encoding="utf-8",
    )

    result = subprocess.run(
        [
            "python3",
            str(SCRIPT),
            "--from-json",
            str(payload),
            "--acceptance-run",
            "--environment-label",
            "offscreen-dev",
            "--os-display",
            "WSL offscreen",
            "--gpu-model",
            "Mesa",
            "--driver-version",
            "software",
            "--qt-version",
            "6.6.3",
            "--cpu-observation",
            "not recorded",
            "--gpu-observation",
            "not recorded",
            "--scroll-note",
            "smooth",
            "--zoom-note",
            "smooth",
            "--channel-visibility-note",
            "smooth",
        ],
        cwd=ROOT,
        env={**os.environ, "QT_QPA_PLATFORM": "offscreen"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )

    data = json.loads(result.stdout)
    assert data["gate"] == "Open"
    assert "QT_QPA_PLATFORM=offscreen" in data["development_reasons"]
    assert "Development-only reasons: QT_QPA_PLATFORM=offscreen" in data["markdown"]


def test_cli_from_json_without_acceptance_run_stays_open(tmp_path):
    payload = tmp_path / "benchmarks.json"
    payload.write_text(
        json.dumps(
            [
                {"channels": 64, "fs": 1000, "seconds": 4, "frames": 120, "fps": 120.0, "gate": "development-signal-only"},
                {"channels": 128, "fs": 1000, "seconds": 4, "frames": 120, "fps": 90.0, "gate": "development-signal-only"},
                {"channels": 256, "fs": 1000, "seconds": 4, "frames": 120, "fps": 56.0, "gate": "development-signal-only"},
            ]
        ),
        encoding="utf-8",
    )

    result = subprocess.run(
        [
            "python3",
            str(SCRIPT),
            "--from-json",
            str(payload),
            "--environment-label",
            "real-linux-gpu",
            "--os-display",
            "Ubuntu 24.04 Wayland",
            "--gpu-model",
            "NVIDIA RTX 4060",
            "--driver-version",
            "555.42",
            "--qt-version",
            "6.6.3",
            "--cpu-observation",
            "12% via system monitor",
            "--gpu-observation",
            "38% via nvidia-smi",
            "--scroll-note",
            "smooth",
            "--zoom-note",
            "smooth",
            "--channel-visibility-note",
            "smooth",
        ],
        cwd=ROOT,
        env={**os.environ, "QT_QPA_PLATFORM": ""},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )

    data = json.loads(result.stdout)
    assert data["gate"] == "Open"
    assert data["development_reasons"] == ["dry-run"]
    assert "Development-only reasons: dry-run" in data["markdown"]
```

- [x] **Step 2: Run tests and verify they fail**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py -q
```

Expected: FAIL because the script has no CLI yet.

- [x] **Step 3: Implement CLI helpers and scenario runner**

Replace `scripts/common/collect_m2_gpu_gate_evidence.py` with this complete content:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import date
import json
import os
from pathlib import Path
import platform
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APP = ROOT / "native" / "build" / "bf_app"
REQUIRED_CHANNELS = (64, 128, 256)


@dataclass(frozen=True)
class BenchmarkResult:
    channels: int
    fs: int
    seconds: int
    frames: int
    fps: float
    producer_gate: str
    command: str = ""


@dataclass(frozen=True)
class EvidenceMetadata:
    date: str
    environment_label: str
    os_display: str
    gpu_model: str
    driver_version: str
    qt_version: str
    cpu_observation: str
    gpu_observation: str
    scroll_note: str
    zoom_note: str
    channel_visibility_note: str


def parse_benchmark_json(text: str, command: str = "") -> BenchmarkResult:
    payload = json.loads(text)
    return BenchmarkResult(
        channels=int(payload["channels"]),
        fs=int(payload["fs"]),
        seconds=int(payload["seconds"]),
        frames=int(payload["frames"]),
        fps=float(payload["fps"]),
        producer_gate=str(payload.get("gate", "")),
        command=command,
    )


def _scenario_map(results: list[BenchmarkResult]) -> dict[int, BenchmarkResult]:
    return {result.channels: result for result in results}


def _has_stall(metadata: EvidenceMetadata) -> bool:
    notes = " ".join(
        [
            metadata.scroll_note,
            metadata.zoom_note,
            metadata.channel_visibility_note,
        ]
    ).lower()
    return "stall" in notes or "卡顿" in notes


def _missing_required_metadata(metadata: EvidenceMetadata) -> bool:
    required = [
        metadata.date,
        metadata.environment_label,
        metadata.os_display,
        metadata.gpu_model,
        metadata.driver_version,
        metadata.qt_version,
        metadata.cpu_observation,
        metadata.gpu_observation,
        metadata.scroll_note,
        metadata.zoom_note,
        metadata.channel_visibility_note,
    ]
    return any(not value.strip() for value in required)


def _has_unrecorded_utilization(metadata: EvidenceMetadata) -> bool:
    return (
        metadata.cpu_observation.strip().lower() == "not recorded"
        or metadata.gpu_observation.strip().lower() == "not recorded"
    )


def classify_gate(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    development_reasons: list[str],
) -> str:
    if development_reasons:
        return "Open"
    if _missing_required_metadata(metadata):
        return "Open"

    scenarios = _scenario_map(results)
    if any(channel not in scenarios for channel in REQUIRED_CHANNELS):
        return "Blocked"
    if any(
        scenarios[channel].fs != 1000 or scenarios[channel].seconds != 4
        for channel in REQUIRED_CHANNELS
    ):
        return "Blocked"
    if _has_stall(metadata):
        return "Blocked"

    fps_256 = scenarios[256].fps
    if fps_256 < 30.0:
        return "Blocked"
    if fps_256 >= 55.0 and not _has_unrecorded_utilization(metadata):
        return "Passed"
    return "Accepted with note"


def format_markdown_evidence(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    gate: str,
    development_reasons: list[str],
) -> str:
    scenarios = sorted(results, key=lambda result: result.channels)
    lines = [
        f"### {metadata.date} {metadata.environment_label}",
        "",
        f"- Gate: {gate}",
        f"- OS/display: {metadata.os_display}",
        f"- GPU: {metadata.gpu_model}, driver {metadata.driver_version}",
        f"- Qt: {metadata.qt_version}",
        f"- CPU: {metadata.cpu_observation}",
        f"- GPU usage: {metadata.gpu_observation}",
    ]
    if development_reasons:
        lines.append(f"- Development-only reasons: {', '.join(development_reasons)}")
    lines.extend(
        [
            "",
            "| Channels | fs | Window | Frames | FPS |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for result in scenarios:
        lines.append(
            f"| {result.channels} | {result.fs} | {result.seconds} | "
            f"{result.frames} | {result.fps:.2f} |"
        )
    lines.extend(
        [
            "",
            "Interaction notes:",
            "",
            f"- Scroll: {metadata.scroll_note}",
            f"- Zoom: {metadata.zoom_note}",
            f"- Channel visibility: {metadata.channel_visibility_note}",
        ]
    )
    commands = [result.command for result in scenarios if result.command]
    if commands:
        lines.extend(["", "Commands:", ""])
        lines.extend(f"- `{command}`" for command in commands)
    return "\n".join(lines)


def detect_development_reasons(env: dict[str, str]) -> list[str]:
    reasons: list[str] = []
    if env.get("QT_QPA_PLATFORM", "").lower() == "offscreen":
        reasons.append("QT_QPA_PLATFORM=offscreen")
    if "microsoft" in platform.uname().release.lower():
        reasons.append("WSL")
    display = env.get("DISPLAY", "")
    wayland = env.get("WAYLAND_DISPLAY", "")
    if display.startswith(":") and not wayland and env.get("XDG_SESSION_TYPE", "").lower() == "xvfb":
        reasons.append("Xvfb")
    return reasons


def default_metadata(args: argparse.Namespace) -> EvidenceMetadata:
    return EvidenceMetadata(
        date=args.date,
        environment_label=args.environment_label,
        os_display=args.os_display,
        gpu_model=args.gpu_model,
        driver_version=args.driver_version,
        qt_version=args.qt_version,
        cpu_observation=args.cpu_observation,
        gpu_observation=args.gpu_observation,
        scroll_note=args.scroll_note,
        zoom_note=args.zoom_note,
        channel_visibility_note=args.channel_visibility_note,
    )


def command_for(app: Path, channels: int, frames: int) -> list[str]:
    return [
        str(app),
        "--smoke-waveform-benchmark",
        "--channels",
        str(channels),
        "--fs",
        "1000",
        "--seconds",
        "4",
        "--frames",
        str(frames),
        "--json",
    ]


def dry_run_results(app: Path, frames: int) -> list[BenchmarkResult]:
    results = []
    for channels in REQUIRED_CHANNELS:
        command = command_for(app, channels, frames)
        results.append(
            BenchmarkResult(
                channels=channels,
                fs=1000,
                seconds=4,
                frames=frames,
                fps=0.0,
                producer_gate="not-run",
                command=" ".join(command),
            )
        )
    return results


def run_benchmarks(app: Path, frames: int) -> list[BenchmarkResult]:
    if not app.exists():
        raise FileNotFoundError(f"benchmark app not found: {app}")
    results = []
    for channels in REQUIRED_CHANNELS:
        command = command_for(app, channels, frames)
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        results.append(parse_benchmark_json(completed.stdout, " ".join(command)))
    return results


def load_results(path: Path) -> list[BenchmarkResult]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError("--from-json must contain a JSON array")
    results = []
    for item in payload:
        results.append(parse_benchmark_json(json.dumps(item), command=str(item.get("command", ""))))
    return results


def emit_payload(
    results: list[BenchmarkResult],
    metadata: EvidenceMetadata,
    gate: str,
    development_reasons: list[str],
) -> dict[str, object]:
    return {
        "gate": gate,
        "development_reasons": development_reasons,
        "metadata": asdict(metadata),
        "results": [asdict(result) for result in sorted(results, key=lambda item: item.channels)],
        "markdown": format_markdown_evidence(results, metadata, gate, development_reasons),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--from-json", type=Path)
    mode.add_argument("--run", action="store_true")
    parser.add_argument("--acceptance-run", action="store_true")
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--date", default=date.today().isoformat())
    parser.add_argument("--environment-label", default="development")
    parser.add_argument("--os-display", default="not recorded")
    parser.add_argument("--gpu-model", default="not recorded")
    parser.add_argument("--driver-version", default="not recorded")
    parser.add_argument("--qt-version", default="not recorded")
    parser.add_argument("--cpu-observation", default="not recorded")
    parser.add_argument("--gpu-observation", default="not recorded")
    parser.add_argument("--scroll-note", default="not recorded")
    parser.add_argument("--zoom-note", default="not recorded")
    parser.add_argument("--channel-visibility-note", default="not recorded")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    metadata = default_metadata(args)
    development_reasons = detect_development_reasons(os.environ) if args.acceptance_run else ["dry-run"]

    if args.dry_run:
        results = dry_run_results(args.app, args.frames)
        development_reasons = ["dry-run"]
    elif args.from_json is not None:
        results = load_results(args.from_json)
    else:
        results = run_benchmarks(args.app, args.frames)

    gate = classify_gate(results, metadata, development_reasons)
    print(json.dumps(emit_payload(results, metadata, gate, development_reasons), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [x] **Step 4: Run CLI and native benchmark tests**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_json_reports_gate_status -q
python3 scripts/common/collect_m2_gpu_gate_evidence.py --dry-run
```

Expected: pytest exits 0; dry-run exits 0 and prints JSON with `"gate": "Open"` and `"development_reasons": ["dry-run"]`.

- [x] **Step 5: Commit**

Run:

```bash
git add scripts/common/collect_m2_gpu_gate_evidence.py tests/scripts/test_m2_gpu_gate_evidence.py
git commit -m "feat: add m2 gpu gate evidence cli"
```

---

### Task 3: M2-4 Runbook And Conservative Status Documentation

**Files:**
- Modify: `docs/reports/m2-rendering-gpu-gate.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/feature-parity-checklist.md`
- Test: `tests/scripts/test_m2_gpu_gate_evidence.py`

**Interfaces:**
- Consumes: `scripts/common/collect_m2_gpu_gate_evidence.py --dry-run` and `--from-json`.
- Produces: documented M2-4 runbook and evidence block format. The feature checklist remains unchecked unless real evidence is recorded.

- [x] **Step 1: Write failing documentation consistency test**

Append this test to `tests/scripts/test_m2_gpu_gate_evidence.py`:

```python
def test_gpu_gate_report_documents_m2_4_runner_and_open_gate():
    report = (ROOT / "docs" / "reports" / "m2-rendering-gpu-gate.md").read_text(encoding="utf-8")

    assert "## M2-4 Runbook" in report
    assert "scripts/common/collect_m2_gpu_gate_evidence.py --run --acceptance-run" in report
    assert "Current gate status: Open." in report
    assert "Development/offscreen results must not be copied as acceptance evidence." in report
```

- [x] **Step 2: Run focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py::test_gpu_gate_report_documents_m2_4_runner_and_open_gate -q
```

Expected: FAIL because `docs/reports/m2-rendering-gpu-gate.md` does not yet contain the M2-4 runbook section.

- [x] **Step 3: Update GPU gate report**

In `docs/reports/m2-rendering-gpu-gate.md`, add this section after `## M2-3 Status`:

```markdown
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

Current gate status: Open.
```

- [x] **Step 4: Update roadmap current phase**

In `docs/roadmap.md`, replace the current phase paragraph with:

```markdown
当前阶段进入 M2-4：真实 GPU/显示门禁证据包、64/128/256 通道 benchmark 聚合、gate 分类和报告格式收口。M2-4 仍不代表完整 M2 验收；只有 `docs/reports/m2-rendering-gpu-gate.md` 记录真实 GPU/显示环境通过或带说明接受后，M2 波形浏览门禁才能关闭。
```

Keep `last_updated: 2026-07-12`.

- [x] **Step 5: Confirm checklist remains conservative**

In `docs/feature-parity-checklist.md`, keep this row unchecked unless a real GPU/display result is recorded as `Passed` or `Accepted with note`:

```markdown
- [x] 真实 GPU/显示环境复测记录已写入 `docs/reports/m2-rendering-gpu-gate.md`
```

If no real GPU run was performed during M2-4 implementation, do not change this file.

- [x] **Step 6: Run documentation tests and commit**

Run:

```bash
python3 -m pytest tests/scripts/test_m2_gpu_gate_evidence.py -q
git diff --check HEAD
```

Expected: tests pass; diff check exits 0.

Commit:

```bash
git add docs/reports/m2-rendering-gpu-gate.md docs/roadmap.md tests/scripts/test_m2_gpu_gate_evidence.py
git commit -m "docs: add m2 gpu gate runbook"
```

---

### Task 4: Full Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-07-12-m2-4-real-gpu-gate.md`

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: verified M2-4 evidence flow on `master` or a feature branch ready to merge. Real GPU gate remains open unless real evidence has been recorded.

- [x] **Step 1: Run full verification**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n tests/scripts -q
./scripts/wsl/verify_m1_foundation.sh --fast
python3 scripts/common/collect_m2_format_evidence.py --dry-run
python3 scripts/common/collect_m2_gpu_gate_evidence.py --dry-run
git diff --check HEAD
```

Expected:

```text
native build exits 0
all non-skipped tests pass
M1 foundation verification passed
format evidence script exits 0
GPU gate dry-run exits 0 and reports Open
git diff --check exits 0
```

- [x] **Step 2: Run a development benchmark sample**

Run:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 64 --fs 1000 --seconds 4 --frames 2 --json
```

Expected: command exits 0 and prints compact JSON with `"channels":64` and `"gate":"development-signal-only"`. Do not record this as acceptance evidence.

- [x] **Step 3: Mark plan execution checkboxes**

In `docs/superpowers/plans/2026-07-12-m2-4-real-gpu-gate.md`, mark completed task steps with `[x]` only after the commands above pass. Leave any real GPU acceptance wording conservative unless a real host run was actually recorded.

- [x] **Step 4: Commit verification status**

Run:

```bash
git add docs/superpowers/plans/2026-07-12-m2-4-real-gpu-gate.md
git commit -m "docs: record m2 gpu gate plan status"
```

---

## Self-Review

Spec coverage:

- Repeatable 64/128/256 benchmark flow is covered by Task 2.
- Machine-readable JSON parsing and report-block formatting are covered by Tasks 1 and 2.
- Host metadata and manual CPU/GPU observation fields are covered by `EvidenceMetadata`.
- Development/offscreen evidence separation is covered by `detect_development_reasons`, classification tests, and Task 3 documentation.
- Conservative roadmap/checklist/report status is covered by Task 3.
- Full verification and dry-run evidence are covered by Task 4.

Deferred-work marker scan:

- The plan contains no deferred implementation markers.
- Every code-changing step includes concrete test code, implementation code, commands, expected results, and commit commands.

Type consistency:

- `BenchmarkResult`, `EvidenceMetadata`, `parse_benchmark_json`, `classify_gate`, and `format_markdown_evidence` are introduced in Task 1 before CLI code uses them in Task 2.
- CLI output fields `gate`, `development_reasons`, `metadata`, `results`, and `markdown` are tested in Task 2 and referenced in Task 3 documentation.
- The M2 gate remains `Open` in documentation unless real GPU evidence is recorded.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-12-m2-4-real-gpu-gate.md`.

Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
