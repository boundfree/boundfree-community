"""Real CSV → sidecar → persistent controller → moving MainWindow viewport."""

import math
import os
from pathlib import Path
import subprocess

import pytest


def test_continuous_playback_with_real_csv_and_sidecar(tmp_path):
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native" / build / config / name
                  for build in ("build-windows", "build")
                  for config in ("Release", "Debug", "")
                  for name in ("bf_app.exe", "bf_app")]
    app = next((candidate for candidate in candidates if candidate.is_file()), None)
    if app is None:
        pytest.skip("请先构建 bf_app")
    fixture = tmp_path / "continuous.csv"
    with fixture.open("w", encoding="utf-8") as stream:
        stream.write("time_s,ch1,ch2\n")
        for sample in range(10000):
            t = sample / 100
            stream.write(f"{t:.2f},{50 * math.sin(2 * math.pi * 8 * t):.6f},"
                         f"{30 * math.cos(2 * math.pi * 13 * t):.6f}\n")
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("PYTHONPATH", str(root / "sidecar"))
    result = subprocess.run([str(app), "--smoke-ui-continuous-file", str(fixture)],
                            cwd=root, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-ui-continuous-file-ok" in result.stdout
    assert "pause=true cache=true" in result.stdout
