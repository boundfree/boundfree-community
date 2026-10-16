"""曲线渲染回归：相同布局基准排除频带底色造成的假阳性。"""

import os
from pathlib import Path
import subprocess

import pytest


def test_spectrum_curves_are_visible_in_both_themes():
    root = Path(__file__).resolve().parents[2]
    candidates = [
        root / "native" / build / config / name
        for build in ("build-windows", "build")
        for config in ("Release", "Debug", "")
        for name in ("spectrum_render_test.exe", "spectrum_render_test")
    ]
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        pytest.skip("请先构建 spectrum_render_test 目标")
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run(
        [str(executable)], cwd=root, env=env, capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    lines = [line for line in result.stdout.splitlines() if "curve_pixels=" in line]
    assert len(lines) == 12, result.stdout
    for line in lines:
        assert int(line.rsplit("=", 1)[1]) >= 200, line
