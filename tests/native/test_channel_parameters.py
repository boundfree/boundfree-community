"""Integrated channel controls: processing isolation, reference mapping and display modes."""
import os
from pathlib import Path
import subprocess
import pytest


def test_channel_parameters():
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native/build/bf_app", root / "native/build-windows/Release/bf_app.exe"]
    app = next((p for p in candidates if p.exists()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    result = subprocess.run([str(app), "--smoke-ui-channel-parameters"], cwd=root,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "amplitude=true filters=true identity=true bipolar=true" in result.stdout
