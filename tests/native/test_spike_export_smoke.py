"""Spike result export smoke CLI contract test.

Tests ``bf_spike --smoke-spike-export <in.csv> <out.csv>``: the native smoke
entry that opens a recording, runs spike analysis, and writes an events CSV
(cluster_id,time_s) via SpikeWindow::exportSpikeResult.

This file only drives the existing smoke entry by subprocess; it does not
read product source text, modify product sources / CMake / golden files, or
run the full test suite. Output paths come from pytest ``tmp_path`` so the
workspace is not polluted.

When the implementation is absent (bf_spike reports ``Unknown option``) the
test skips, avoiding spurious red during TDD.
"""

import os
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]

# A high-sampling-rate CSV with obvious spikes; generated once and committed
# under tests/fixtures/generated so the test is deterministic.
SPIKE_FIXTURE = ROOT / "tests" / "fixtures" / "generated" / "spike_highrate.csv"


def _candidate_apps() -> list[Path]:
    return [
        ROOT / "native" / "build-windows" / "Release" / "bf_spike.exe",
        ROOT / "native" / "build-windows" / "Debug" / "bf_spike.exe",
        ROOT / "native" / "build" / "Release" / "bf_spike.exe",
        ROOT / "native" / "build" / "Debug" / "bf_spike.exe",
        ROOT / "native" / "build" / "bf_spike.exe",
        ROOT / "native" / "build" / "bf_spike",
    ]


def _resolve_app() -> Path:
    existing = [path for path in _candidate_apps() if path.exists()]
    if not existing:
        listed = "\n".join(f"- {path}" for path in _candidate_apps())
        pytest.skip(f"bf_spike executable not found, build native first.\nChecked:\n{listed}")
    return existing[0]


APP = _resolve_app()


def _run(args, check: bool = False) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    return subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def _flag_is_unsupported(result: subprocess.CompletedProcess, flag: str) -> bool:
    return result.returncode != 0 and "Unknown option" in (result.stdout + result.stderr) and flag in (result.stdout + result.stderr)


def test_smoke_spike_export_writes_events_csv(tmp_path: Path):
    """Open -> analyze -> exportSpikeResult -> assert CSV header and rows>0."""
    if not SPIKE_FIXTURE.exists():
        pytest.skip(f"spike fixture missing: {SPIKE_FIXTURE}")

    out_csv = tmp_path / "spike_out.csv"
    result = _run([str(APP), "--smoke-spike-export", str(SPIKE_FIXTURE), str(out_csv)])

    if _flag_is_unsupported(result, "--smoke-spike-export"):
        pytest.skip("--smoke-spike-export not implemented yet")

    assert result.returncode == 0, (
        f"smoke-spike-export exited {result.returncode}\nstdout={result.stdout}\nstderr={result.stderr}"
    )
    assert "smoke-spike-export-ok rows=" in result.stdout, (
        f"missing ok marker in stdout: {result.stdout!r}"
    )

    # Parse the rows count from the ok line.
    ok_line = [ln for ln in result.stdout.splitlines() if ln.startswith("smoke-spike-export-ok")][0]
    rows = int(ok_line.split("rows=")[1].split()[0])
    assert rows > 0, f"expected positive row count, got {rows}"

    # CSV exists, first line is header, data rows == rows.
    assert out_csv.exists(), f"output CSV not created: {out_csv}"
    lines = out_csv.read_text(encoding="utf-8").splitlines()
    assert lines, "output CSV is empty"
    assert lines[0].strip() == "cluster_id,time_s", f"unexpected header: {lines[0]!r}"
    data_lines = [ln for ln in lines[1:] if ln.strip()]
    assert len(data_lines) == rows, f"row count mismatch: header claims {rows}, file has {len(data_lines)}"

    # Each data row has exactly two comma-separated fields; times ascending.
    times = []
    for ln in data_lines:
        parts = ln.split(",")
        assert len(parts) == 2, f"expected 2 columns, got {parts!r}"
        cid = int(parts[0])
        t = float(parts[1])
        assert cid >= 0
        times.append(t)
    assert times == sorted(times), "times not in ascending order"
