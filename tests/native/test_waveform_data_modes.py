"""Raw/processed waveform and analysis input switching."""
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def run_smoke(*args, timeout=60):
    candidates = [ROOT / "native/build/bf_app", ROOT / "native/build-windows/Release/bf_app.exe"]
    app = next((path for path in candidates if path.exists()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    result = subprocess.run([str(app), *map(str, args)], cwd=ROOT,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, timeout=timeout)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def test_waveform_raw_processed_switch():
    assert "waveform-data-modes-ok" in run_smoke("--smoke-ui-waveform-data-modes")
