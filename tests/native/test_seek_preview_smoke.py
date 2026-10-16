"""Regression for real MainWindow slider preview, timer and seek dispatch."""

import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_seek_preview_does_not_dispatch_or_get_overwritten():
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
        [str(app), "--smoke-ui-seek-preview"], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=20,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-ui-seek-preview-ok" in result.stdout
    for check in ("preview", "timer", "release_once", "keyboard", "track", "refresh", "file_switch"):
        assert f"{check}=true" in result.stdout
