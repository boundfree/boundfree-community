"""Regression for cached and asynchronously loaded waveform scrub previews."""

import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_scrub_preview_coalesces_and_rejects_stale_results():
    candidates = [
        ROOT / "native/build/bf_app",
        ROOT / "native/build/Release/bf_app.exe",
        ROOT / "native/build-windows/Release/bf_app.exe",
    ]
    app = next((path for path in candidates if path.exists()), None)
    if app is None:
        pytest.skip("Build bf_app before running native UI regression tests")
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    result = subprocess.run(
        [str(app), "--smoke-ui-scrub-preview"], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=20,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-ui-seek-preview-ok" in result.stdout
    for check in ("cache", "coalesced", "stale", "loading", "release_once", "error", "failed_open", "analysis"):
        assert f"{check}=true" in result.stdout


def test_scrub_preview_with_real_csv_and_sidecar(tmp_path):
    candidates = [ROOT / "native" / build / config / name
                  for build in ("build", "build-windows")
                  for config in ("", "Release", "Debug")
                  for name in ("bf_app", "bf_app.exe")]
    app = next((path for path in candidates if path.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app before running native UI regression tests")
    fixture = tmp_path / "scrub.csv"
    with fixture.open("w", encoding="utf-8") as stream:
        stream.write("time_s,ch1,ch2\n")
        for sample in range(10000):
            # A ramp makes distant slices contain different physical samples.
            stream.write(f"{sample / 100:.2f},{sample / 10:.1f},{-sample / 20:.2f}\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    result = subprocess.run(
        [str(app), "--smoke-ui-scrub-file", str(fixture)], cwd=ROOT, env=env,
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=25,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-ui-scrub-file-ok remote=true coalesced=true stale=true final=true requests=4" in result.stdout
