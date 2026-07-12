import os
import importlib.util
import json
import subprocess
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "collect_m2_gpu_gate_evidence.py"

spec = importlib.util.spec_from_file_location("collect_m2_gpu_gate_evidence", SCRIPT)
gpu_gate = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = gpu_gate
spec.loader.exec_module(gpu_gate)


def _result(channels: int, fps: float, *, frames: int = 120, producer_gate: str = "display-candidate"):
    return gpu_gate.BenchmarkResult(
        channels=channels,
        fs=1000,
        seconds=4,
        frames=frames,
        fps=fps,
        producer_gate=producer_gate,
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


def test_classify_gate_opens_when_required_scenario_reports_development_signal_only():
    gate = gpu_gate.classify_gate(
        [
            _result(64, 120.0),
            _result(128, 90.0),
            _result(256, 70.0, producer_gate="development-signal-only"),
        ],
        _metadata(),
        [],
    )

    assert gate == "Open"


def test_classify_gate_blocks_when_required_scenario_frames_are_not_120():
    gate = gpu_gate.classify_gate(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 70.0, frames=119)],
        _metadata(),
        [],
    )

    assert gate == "Blocked"


def test_classify_gate_opens_when_required_metadata_is_not_recorded():
    gate = gpu_gate.classify_gate(
        [_result(64, 120.0), _result(128, 90.0), _result(256, 70.0)],
        _metadata(scroll_note="not recorded"),
        [],
    )

    assert gate == "Open"


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


def test_cli_from_json_acceptance_run_remains_open_for_imported_evidence(tmp_path):
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
    assert "from-json-import" in data["development_reasons"]
    assert any("from-json-import" in line for line in data["markdown"].splitlines())


def test_cli_from_json_acceptance_run_marks_mesa_software_renderer_as_open(tmp_path):
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
            "mesa-offscreen-dev",
            "--os-display",
            "Ubuntu 24.04 Wayland",
            "--gpu-model",
            "Mesa",
            "--driver-version",
            "software",
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
    assert "from-json-import" in data["development_reasons"]
    assert any("Mesa/software" in reason for reason in data["development_reasons"])
    assert any("Mesa/software" in line for line in data["markdown"].splitlines())


def test_detect_development_reasons_marks_display_99_as_xvfb():
    reasons = gpu_gate.detect_development_reasons(
        {"DISPLAY": ":99"},
        gpu_model="NVIDIA",
        driver_version="555",
    )

    assert "Xvfb" in reasons


def test_gpu_gate_report_documents_m2_4_runner_and_open_gate():
    report = (ROOT / "docs" / "reports" / "m2-rendering-gpu-gate.md").read_text(encoding="utf-8")

    assert "## M2-4 Runbook" in report
    assert "scripts/collect_m2_gpu_gate_evidence.py --run --acceptance-run" in report
    assert "Current gate status: Open." in report
    assert "Development/offscreen results must not be copied as acceptance evidence." in report
