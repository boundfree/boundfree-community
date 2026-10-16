"""MainWindow clock/cache integration with controlled IO latency."""
import os
from pathlib import Path
import subprocess
import pytest


def test_continuous_playback_cache_and_state_boundaries():
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native" / build / config / name
                  for build in ("build-windows", "build")
                  for config in ("Release", "Debug", "")
                  for name in ("bf_app.exe", "bf_app")]
    app = next((p for p in candidates if p.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    result = subprocess.run([str(app), "--smoke-ui-continuous-playback"], cwd=root,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smooth=true cache=true pause=true speed=true tail=true loading=true short_response=true" in result.stdout
