"""Playback retains its completed analysis surface while the next result is pending."""

import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_playback_analysis_refresh_preserves_ready_result_until_replacement():
    candidates = [ROOT / "native" / build / config / name
                  for build in ("build", "build-windows")
                  for config in ("", "Release", "Debug")
                  for name in ("bf_app", "bf_app.exe")]
    app = next((path for path in candidates if path.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app before running native UI regression tests")
    result = subprocess.run(
        [str(app), "--smoke-ui-analysis-refresh"], cwd=ROOT,
        env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "analysis-refresh-ok background=true preserved=true atomic=true stale=true explicit=true timer=true" in result.stdout
